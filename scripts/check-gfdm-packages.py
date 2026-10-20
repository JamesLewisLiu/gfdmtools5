"""Check the five distributable archives and their launcher/identity wiring."""
import pathlib
import sys
import xml.etree.ElementTree as ET
import zipfile


def check(directory):
    hook = None
    for version, letter, spec in [(4, "G", "A"), (5, "H", "A"),
                                  (6, "I", "A"), (7, "J", "B"), (8, "K", "B")]:
        prefix = f"gfdm-v{version}"
        with zipfile.ZipFile(directory / f"{prefix}.zip") as package:
            names = package.namelist()
            expected = {"gfdmhook1.dll", "config.exe", "eamio.dll", "geninput.dll",
                        "inject.exe", "config-gf.bat", "config-dm.bat",
                        f"{prefix}-boot.xml", f"{prefix}.conf"}
            for mode in ("gf", "dm"):
                expected.update({f"gamestart-v{version}-{mode}.bat",
                                 f"{prefix}-ea3-ident-{mode}.xml", f"{prefix}-{mode}.conf"})
            assert len(names) == len(set(names)) and set(names) == expected, names
            binary = package.read("gfdmhook1.dll")
            assert binary[:2] == b"MZ"
            if hook is not None:
                assert binary == hook, "all versions must ship the same common hook"
            hook = binary
            # Expat does not accept a multi-byte encoding declaration on bytes.
            ET.fromstring(package.read(f"{prefix}-boot.xml").decode("shift_jis"))
            for mode, number, switch in [("gf", 33, "g"), ("dm", 32, "d")]:
                root = ET.fromstring(package.read(f"{prefix}-ea3-ident-{mode}.xml").decode("shift_jis"))
                identity = [root.findtext("soft/" + key) for key in
                            ("model", "dest", "spec", "rev")]
                assert identity == [f"{letter}{number}", "J", spec, "A"], identity
                config = dict(line.split("=", 1) for line in
                              package.read(f"{prefix}-{mode}.conf").decode().splitlines()
                              if line and not line.startswith("#"))
                assert config["security.mcode"] == f"GC{letter}{number}J{spec}A"
                assert len(config["eamuse.pcbid"]) == 20
                launch = package.read(f"gamestart-v{version}-{mode}.bat").decode()
                for token in (f"gdv{version}.exe -{switch}",
                              f"-I {prefix}-ea3-ident-{mode}.xml",
                              f"-b {prefix}-boot.xml", f"--config {prefix}-{mode}.conf"):
                    assert token in launch, (version, mode, token)
            print(f"PASS {prefix}: files, shared hook, GF/DM model and launcher wiring")


if __name__ == "__main__":
    check(pathlib.Path(sys.argv[1]))
