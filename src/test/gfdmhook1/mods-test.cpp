#undef NDEBUG
#include <windows.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <tuple>
#include <thread>
#include "gfdmhook1/mod-codec.h"
#include "gfdmhook1/ifs/texbin.hpp"
#include "gfdmhook1/ifs/3rd_party/lodepng.h"
#include "gfdmhook1/ifs/3rd_party/rapidxml.hpp"
namespace fs=std::filesystem;
static unsigned reads,warnings;
static std::vector<unsigned char> base;
static std::vector<unsigned char> bytes(const fs::path &p)
{std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void write(const fs::path &p,const std::string &s)
{fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);assert(f.write(s.data(),s.size()));}
static void png(const fs::path &p,unsigned char r,unsigned char g,unsigned char b,unsigned w=4,unsigned h=4)
{
    fs::create_directories(p.parent_path());std::vector<unsigned char> image(w*h*4);
    for(unsigned i=0;i<w*h;i++){image[i*4]=r;image[i*4+1]=g;image[i*4+2]=b;image[i*4+3]=255;}
    assert(lodepng::encode(p.string(),image,w,h)==0);
}
static int read_base(void *,unsigned char **out,size_t *size)
{reads++;*size=base.size();*out=(unsigned char*)malloc(base.size()?base.size():1);assert(*out);memcpy(*out,base.data(),base.size());return base.empty()?0:1;}
static void log(int level,const char *){if(level<=1)warnings++;}
static bool convert(const fs::path &root,const char *name,char path[1024])
{gfdm_mod_backend backend{nullptr,read_base,nullptr,free,log};return gfdm_mod_convert(root.string().c_str(),name,&backend,path,1024);}
static void pixel(const char *path,const char *name,unsigned char red,unsigned char green,unsigned char blue)
{
    auto texture=Texbin::from_path(path);assert(texture&&texture->images.count(name));
    auto decoded=texture->images.at(name).tex_to_argb8888();assert(decoded);
    assert(std::get<1>(*decoded)==4&&std::get<2>(*decoded)==4);
    auto &v=std::get<0>(*decoded);assert(v.size()==64);
    for(unsigned i=0;i<16;i++){assert(v[i*4]==red&&v[i*4+1]==green&&v[i*4+2]==blue&&v[i*4+3]==255);}
}
static uint64_t identity(const char *path)
{
    HANDLE file=CreateFileA(path,0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    assert(file!=INVALID_HANDLE_VALUE);BY_HANDLE_FILE_INFORMATION info{};assert(GetFileInformationByHandle(file,&info));CloseHandle(file);
    return (uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow;
}
int main(int argc,char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc==3&&!strcmp(argv[1],"--xml-worker")) {
        fs::path root=argv[2];base=bytes(root.parent_path()/"base.xml");char path[1024];
        assert(convert(root,"product/music.xml",path));return 0;
    }
    assert(argc==1);
    char temp[MAX_PATH],unique[MAX_PATH];assert(GetTempPathA(sizeof(temp),temp));
    assert(GetTempFileNameA(temp,"gfm",0,unique));assert(DeleteFileA(unique));
    fs::path root=fs::path(unique)/"data_mods";fs::create_directories(root/"a_mod");fs::create_directories(root/"b_mod");
    auto original=root.parent_path()/"original.png";png(original,255,0,0);
    Texbin source;assert(source.add_or_replace_image("FACE",original.string().c_str()));
    assert(source.add_or_replace_image("KEEP",original.string().c_str()));
    source.rects.emplace("SMALL",RectEntryParsed{"FACE",1,1,2,2});
    auto bin=root.parent_path()/"original.bin";assert(source.save(bin.string().c_str()));base=bytes(bin);
    png(root/"a_mod/product/picture/FACE.png",0,255,0);
    png(root/"b_mod/product/picture/tex/FACE.png",0,0,255);
    png(root/"b_mod/product/picture/EXTRA.png",255,255,0);
    char path[1024];assert(convert(root,"product/picture.bin",path));
    pixel(path,"FACE",0,255,0);pixel(path,"KEEP",255,0,0);pixel(path,"EXTRA",255,255,0);
    png(root/"a_mod/product/picture/SMALL.png",0,255,255,2,2);
    assert(convert(root,"product/picture.bin",path));
    auto atlas=Texbin::from_path(path);assert(atlas&&atlas->rects.count("SMALL"));
    auto region=atlas->images.at("FACE").tex_to_argb8888();assert(region);
    for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++) {
        auto &data=std::get<0>(*region);size_t i=(y*4+x)*4;
        assert(data[i]==0&&data[i+1]==255&&data[i+2]==((x>=1&&x<3&&y>=1&&y<3)?255:0)&&data[i+3]==255);
    }
    fs::remove(root/"a_mod/product/picture/SMALL.png");assert(convert(root,"product/picture.bin",path));
    auto first=fs::last_write_time(path);unsigned previous_reads=reads;std::string cached=path;
    assert(convert(root,"product/picture.bin",path)&&cached==path&&reads==previous_reads&&fs::last_write_time(path)==first);
    fs::remove(root/"a_mod/product/picture/FACE.png");
    assert(convert(root,"product/picture.bin",path));pixel(path,"FACE",0,0,255);
    write(path,"corrupt cache");assert(convert(root,"product/picture.bin",path));pixel(path,"FACE",0,0,255);
    Texbin replacement;assert(replacement.add_or_replace_image("FULL",original.string().c_str()));
    assert(replacement.save((root/"a_mod/product/picture.bin").string().c_str()));
    assert(convert(root,"product/picture.bin",path));pixel(path,"FULL",255,0,0);pixel(path,"FACE",0,0,255);
    assert(!Texbin::from_path(path)->images.count("KEEP"));
    auto broken=root/"b_mod/product/picture/EXTRA.png";write(broken,"not a PNG");
    assert(!convert(root,"product/picture.bin",path)&&warnings>0);
    png(broken,255,255,0);assert(convert(root,"product/picture.bin",path));
    std::string xml="<?xml version=\"1.0\"?><mdb version=\"one\"><music id=\"1\"/></mdb>";
    write(root.parent_path()/"base.xml",xml);
    base.assign(xml.begin(),xml.end());
    write(root/"a_mod/product/music.merged.xml","<?xml version=\"1.0\"?><mdb><music id=\"2\">A &amp; B</music></mdb>");
    write(root/"b_mod/product/music.merged.xml","<?xml version=\"1.0\"?><mdb><music id=\"3\"/></mdb>");
    assert(convert(root,"product/music.xml",path));auto merged=bytes(path);merged.push_back(0);
    rapidxml::xml_document<> doc;doc.parse<0>((char*)merged.data());
    auto music=doc.first_node("mdb")->first_node("music");
    assert(!strcmp(music->first_attribute("id")->value(),"1"));music=music->next_sibling("music");
    assert(!strcmp(music->first_attribute("id")->value(),"2")&&!strcmp(music->value(),"A & B"));music=music->next_sibling("music");
    assert(!strcmp(music->first_attribute("id")->value(),"3")&&!music->next_sibling("music"));
    assert(!strcmp(doc.first_node("mdb")->first_attribute("version")->value(),"one"));
    auto cached_identity=identity(path);
    char self[MAX_PATH];assert(GetModuleFileNameA(nullptr,self,sizeof(self)));
    std::string command="\""+std::string(self)+"\" --xml-worker \""+root.string()+"\"";
    STARTUPINFOA startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    assert(CreateProcessA(self,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process));
    assert(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0);
    DWORD code=1;assert(GetExitCodeProcess(process.hProcess,&code)&&code==0);CloseHandle(process.hProcess);CloseHandle(process.hThread);
    assert(identity(path)==cached_identity);
    std::string concurrent_path=path;std::vector<std::thread> workers;
    for(unsigned i=0;i<8;i++)workers.emplace_back([&](){char result[1024];assert(convert(root,"product/music.xml",result)&&result==concurrent_path);});
    for(auto &worker:workers)worker.join();
    write(root/"b_mod/product/music.merged.xml","<wrong><music id=\"4\"/></wrong>");
    assert(!convert(root,"product/music.xml",path));
    fs::remove(root/"b_mod/product/music.merged.xml");
    write(root/"a_mod/product/music.xml","<mdb><music id=\"9\"/></mdb>");
    assert(convert(root,"product/music.xml",path));merged=bytes(path);std::string text(merged.begin(),merged.end());
    assert(text.find("id=\"9\"")<text.find("id=\"2\"")&&text.find("id=\"1\"")==std::string::npos);
    fs::remove(root/"a_mod/product/music.merged.xml");assert(!convert(root,"product/music.xml",path));
    base.clear();png(root/"a_mod/product/new/NEW.png",0,255,0);
    assert(convert(root,"product/new.bin",path));pixel(path,"NEW",0,255,0);
    assert(!convert(root,"../outside.xml",path));assert(!convert(root,"/data/music.xml",path));
    assert(bytes(bin).size()>0&&fs::exists(original));
    /* The unique fixture path is owned by this test and checked before cleanup. */
    assert(fs::canonical(root.parent_path())==fs::canonical(unique));
    assert(fs::equivalent(root.parent_path().parent_path(),fs::path(temp)));
    fs::remove_all(root.parent_path());
    std::cout<<"PASS: PNG texbin pixels/atlas/new-file/priority/add/remove/full-base/cache/corruption, ordered XML merge/root rejection/entities, cold-process cache reuse, concurrent opens and protected original files\n";
}
