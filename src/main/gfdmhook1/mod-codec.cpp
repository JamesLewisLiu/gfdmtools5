#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include "gfdmhook1/mod-codec.h"
#include "ifs/texbin.hpp"
#include "ifs/3rd_party/lodepng.h"
#include "ifs/log.hpp"
#include "ifs/3rd_party/rapidxml.hpp"
#include "ifs/3rd_party/rapidxml_print.hpp"

config_t::config_t() {}
config_t config;
static thread_local const gfdm_mod_backend *current_backend;
struct BackendScope {
    const gfdm_mod_backend *previous;
    explicit BackendScope(const gfdm_mod_backend *value):previous(current_backend){current_backend=value;}
    ~BackendScope(){current_backend=previous;}
};
static void warn(const char *,const char *format,...)
{
    char text[2048];va_list args;va_start(args,format);
    vsnprintf(text,sizeof(text),format,args);va_end(args);
    if(current_backend && current_backend->log)current_backend->log(1,text);
}
static void detail(const char *,const char *format,...)
{
    char text[2048];va_list args;va_start(args,format);
    vsnprintf(text,sizeof(text),format,args);va_end(args);
    if(current_backend && current_backend->log)current_backend->log(3,text);
}
log_formatter_t imp_log_body_fatal=warn,imp_log_body_warning=warn;
log_formatter_t imp_log_body_info=detail,imp_log_body_misc=detail;

namespace {
namespace fs=std::filesystem;
#define GFDM_CODEC_STRING2(x) #x
#define GFDM_CODEC_STRING(x) GFDM_CODEC_STRING2(x)
#ifndef GITREV
#define GITREV standalone
#endif
const std::string codec_version="codec2|" GFDM_CODEC_STRING(GITREV) "|";
constexpr size_t limit=128*1024*1024;
std::mutex codec_mutex;
struct Entry {std::string plan,output,stamp;};
std::map<std::string,Entry> memory_cache;
std::set<std::string> reported_errors;
std::string lower(std::string value)
{for(char &c:value)if(c>='A'&&c<='Z')c+='a'-'A';return value;}
std::string stamp(const fs::path &path)
{
    HANDLE h=CreateFileA(path.string().c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return {};
    BY_HANDLE_FILE_INFORMATION i{};bool ok=GetFileInformationByHandle(h,&i)!=0;CloseHandle(h);
    if(!ok)return {};
    return fmt::format("{}:{}:{}:{}:{}:{}:{}",i.dwVolumeSerialNumber,i.nFileIndexHigh,
        i.nFileIndexLow,i.nFileSizeHigh,i.nFileSizeLow,i.ftLastWriteTime.dwHighDateTime,i.ftLastWriteTime.dwLowDateTime);
}
std::string hash(const void *data,size_t size)
{
    HCRYPTPROV provider=0;HCRYPTHASH h=0;std::array<unsigned char,32> digest{};DWORD n=digest.size();
    if(size>MAXDWORD || !CryptAcquireContextA(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT))
        throw std::runtime_error("Cannot initialize mod cache hash");
    bool ok=CryptCreateHash(provider,CALG_SHA_256,0,0,&h) &&
        CryptHashData(h,static_cast<const BYTE*>(data),static_cast<DWORD>(size),0) &&
        CryptGetHashParam(h,HP_HASHVAL,digest.data(),&n,0);
    if(h)CryptDestroyHash(h);CryptReleaseContext(provider,0);
    if(!ok)throw std::runtime_error("Cannot hash mod cache input");
    std::string out;for(unsigned char b:digest)out+=fmt::format("{:02x}",b);return out;
}
std::vector<unsigned char> read(const fs::path &path)
{
    auto size=fs::file_size(path);if(size>limit)throw std::runtime_error("Mod input exceeds 128 MiB");
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot read mod input");
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    if(size && !f.read(reinterpret_cast<char*>(bytes.data()),size))throw std::runtime_error("Short mod input read");
    return bytes;
}
bool regular(const fs::path &path)
{
    DWORD a=GetFileAttributesA(path.string().c_str());
    return a!=INVALID_FILE_ATTRIBUTES && !(a&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
}
std::vector<fs::path> folders(const fs::path &root)
{
    std::vector<fs::path> result;std::error_code error;
    for(const auto &entry:fs::directory_iterator(root,error)) {
        DWORD a=GetFileAttributesA(entry.path().string().c_str());
        if(a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_DIRECTORY) &&
            !(a&FILE_ATTRIBUTE_REPARSE_POINT) && lower(entry.path().filename().string())!="_cache")result.push_back(entry.path());
    }
    std::sort(result.begin(),result.end(),[](const auto &a,const auto &b){return lower(a.string())<lower(b.string());});
    return result;
}
void atomic_text(const fs::path &path,const std::string &text)
{
    auto temp=path.string()+fmt::format(".{}.{}.tmp",GetCurrentProcessId(),GetCurrentThreadId());
    {std::ofstream out(temp,std::ios::binary);if(!out || !out.write(text.data(),text.size()))throw std::runtime_error("Cannot write mod cache");}
    if(!MoveFileExA(temp.c_str(),path.string().c_str(),MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(temp.c_str());throw std::runtime_error("Cannot publish mod cache");
    }
}
rapidxml::xml_node<> *xml_root(rapidxml::xml_document<> &doc)
{
    rapidxml::xml_node<> *result=nullptr;
    for(auto n=doc.first_node();n;n=n->next_sibling())if(n->type()==rapidxml::node_element) {
        if(result)throw std::runtime_error("XML must have one root element");result=n;
    }
    if(!result)throw std::runtime_error("XML has no root element");return result;
}
std::vector<char> xml_bytes(const std::vector<unsigned char> &bytes,const gfdm_mod_backend &backend)
{
    if(!bytes.empty()&&bytes.front()==0xa0) {
        unsigned char *decoded=nullptr;size_t size=0;
        if(!backend.xml_text || !backend.release || !backend.xml_text(bytes.data(),bytes.size(),&decoded,&size))
            throw std::runtime_error("Cannot decode binary XML");
        std::vector<char> result(reinterpret_cast<char*>(decoded),reinterpret_cast<char*>(decoded)+size);
        backend.release(decoded);result.push_back(0);return result;
    }
    std::vector<char> result(bytes.begin(),bytes.end());result.push_back(0);return result;
}
}

extern "C" bool gfdm_mod_convert(const char *root,const char *relative,
    const gfdm_mod_backend *backend,char *output,size_t capacity)
{
    if(!root||!relative||!backend||!output)return false;
    std::lock_guard<std::mutex> lock(codec_mutex);BackendScope scope(backend);
    std::string plan;
    try {
        std::string rel=relative;std::replace(rel.begin(),rel.end(),'\\','/');
        if(rel.empty()||rel.front()=='/'||rel.find(':')!=std::string::npos)return false;
        for(const auto &part:fs::path(rel))if(part==".."||part==".")return false;
        std::string extension=lower(fs::path(rel).extension().string());
        if(extension!=".bin"&&extension!=".xml")return false;
        auto mods=folders(root);fs::path complete;std::map<std::string,fs::path> pngs;
        std::vector<fs::path> merges;
        auto stem=fs::path(rel);stem.replace_extension();auto merge_path=fs::path(rel);merge_path.replace_extension(".merged.xml");
        for(const auto &mod:mods) {
            if(complete.empty()&&regular(mod/rel))complete=mod/rel;
            if(extension==".xml"&&regular(mod/merge_path))merges.push_back(mod/merge_path);
            if(extension==".bin")for(const auto &folder:{mod/stem,mod/stem/"tex"}) {
                std::error_code error;
                for(const auto &entry:fs::directory_iterator(folder,error))if(regular(entry.path())&&lower(entry.path().extension().string())==".png") {
                    auto name=entry.path().stem().string();for(char &c:name)if(c>='a'&&c<='z')c+='A'-'a';
                    pngs.emplace(name,entry.path());
                }
            }
        }
        if(pngs.empty()&&merges.empty())return false;
        std::vector<fs::path> inputs;if(!complete.empty())inputs.push_back(complete);
        for(const auto &[name,path]:pngs)inputs.push_back(path);
        inputs.insert(inputs.end(),merges.begin(),merges.end());
        plan=lower(fs::absolute(root).string()+"/"+rel)+"|"+codec_version;
        for(const auto &path:inputs)plan+=path.string()+":"+stamp(path)+"|";
        auto key=lower(fs::absolute(root).string()+"/"+rel);
        auto found=memory_cache.find(key);
        if(found!=memory_cache.end()&&found->second.plan==plan&&stamp(found->second.output)==found->second.stamp&&regular(found->second.output)) {
            if(found->second.output.size()>=capacity)return false;
            memcpy(output,found->second.output.c_str(),found->second.output.size()+1);return true;
        }
        std::vector<unsigned char> base;
        if(!complete.empty())base=read(complete);
        else {
            unsigned char *data=nullptr;size_t size=0;
            if(!backend->read_base||!backend->release)throw std::runtime_error("No native mod base reader");
            int result=backend->read_base(backend->context,&data,&size);
            if(result<0)throw std::runtime_error("Native mod base read failed");
            if(result>0) {if(size>limit){backend->release(data);throw std::runtime_error("Mod base exceeds 128 MiB");}base.assign(data,data+size);backend->release(data);}
        }
        std::string fingerprint=plan+hash(base.data(),base.size());
        auto generated=fs::path(root)/"_cache"/"generated";
        fs::create_directories(generated);
        auto destination=generated/(hash(fingerprint.data(),fingerprint.size())+extension);
        auto checksum=destination.string()+".sha256";
        if(regular(destination)&&regular(checksum)&&fs::file_size(checksum)==64) {
            auto content=read(destination),expected=read(checksum);
            if(hash(content.data(),content.size())==std::string(expected.begin(),expected.end())) {
                auto path=destination.string();if(path.size()>=capacity)return false;
                memory_cache[key]={plan,path,stamp(destination)};
                memcpy(output,path.c_str(),path.size()+1);return true;
            }
        }
        std::string written;
        if(extension==".xml") {
            if(base.empty())throw std::runtime_error("Merged XML requires a base document");
            std::list<std::vector<char>> storage;storage.push_back(xml_bytes(base,*backend));
            rapidxml::xml_document<> document;document.parse<rapidxml::parse_declaration_node|rapidxml::parse_no_utf8>(storage.back().data());
            auto target=xml_root(document);
            for(const auto &path:merges) {
                storage.push_back(xml_bytes(read(path),*backend));rapidxml::xml_document<> fragment;
                fragment.parse<rapidxml::parse_declaration_node|rapidxml::parse_no_utf8>(storage.back().data());auto source=xml_root(fragment);
                if(std::string(source->name(),source->name_size())!=std::string(target->name(),target->name_size()))throw std::runtime_error("Merged XML root differs from base");
                for(auto child=source->first_node();child;child=child->next_sibling())target->append_node(document.clone_node(child));
            }
            rapidxml::print(std::back_inserter(written),document,0);atomic_text(destination,written);
        } else {
            Texbin texture;
            if(!base.empty()) {
                std::istringstream stream(std::string(reinterpret_cast<const char*>(base.data()),base.size()));
                auto original=Texbin::from_stream(stream);if(!original)throw std::runtime_error("PNG target is not a supported texbin");texture=std::move(*original);
            }
            for(const auto &[name,path]:pngs) {
                auto input=read(path);LodePNGState state;lodepng_state_init(&state);
                unsigned width=0,height=0;
                unsigned error=lodepng_inspect(&width,&height,&state,input.data(),input.size());
                lodepng_state_cleanup(&state);
                if(error||!width||!height||static_cast<uint64_t>(width)*height*4>64*1024*1024)
                    throw std::runtime_error("Invalid or oversized PNG replacement");
                if(!texture.add_or_replace_image(name.c_str(),path.string().c_str()))throw std::runtime_error("PNG replacement rejected");
            }
            auto temporary=destination.string()+fmt::format(".{}.tmp",GetCurrentProcessId());
            if(!texture.save(temporary.c_str()))throw std::runtime_error("Cannot encode texbin cache");
            if(!MoveFileExA(temporary.c_str(),destination.string().c_str(),MOVEFILE_REPLACE_EXISTING)){DeleteFileA(temporary.c_str());throw std::runtime_error("Cannot publish texbin cache");}
        }
        /* Changes while rebuilding are retried on the next open, not cached. */
        auto encoded=read(destination);atomic_text(checksum,hash(encoded.data(),encoded.size()));
        std::string after=lower(fs::absolute(root).string()+"/"+rel)+"|"+codec_version;
        for(const auto &path:inputs)after+=path.string()+":"+stamp(path)+"|";
        if(after!=plan)throw std::runtime_error("Mod input changed during conversion");
        auto path=destination.string();if(path.size()>=capacity)throw std::runtime_error("Mod cache path is too long");
        memory_cache[key]={plan,path,stamp(destination)};
        memcpy(output,path.c_str(),path.size()+1);return true;
    } catch(const std::exception &error) {
        if(reported_errors.insert(plan+error.what()).second&&backend->log)backend->log(1,error.what());
        return false;
    } catch(...) {if(backend->log)backend->log(1,"Unknown mod converter failure");return false;}
}
