"""Check the five distributable archives and their launcher/identity wiring."""
import hashlib
import pathlib
import sys
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
                        "CLVSD.ax"}
            for mode in ("gf", "dm"):
                expected.update({f"gamestart-v{version}-{mode}.bat",
                                 f"{prefix}-{mode}.conf"})
            assert len(names) == len(set(names)) and set(names) == expected, names
            assert not any(name.lower().endswith(".xml") for name in names), names
            binary = package.read("gfdmhook1.dll")
            assert binary[:2] == b"MZ"
            decoder = package.read("CLVSD.ax")
            expected_decoder = (
                "A166092B159F991764CCD4BEF9EAED2B016CFD71A8A89C74818BA54C60650CAF"
                if version >= 7 else
                "C7BA1DFF716A4C6CF0EC3CFCB5900C3206EB6EC042E858BAEBD11F159C06EEA2"
            )
            assert hashlib.sha256(decoder).hexdigest().upper() == expected_decoder
            assert decoder[:2] == b"MZ"
            if hook is not None:
                assert binary == hook, "all versions must ship the same common hook"
            hook = binary
            for mode, number, switch in [("gf", 33, "g"), ("dm", 32, "d")]:
                config = dict(line.split("=", 1) for line in
                              package.read(f"{prefix}-{mode}.conf").decode().splitlines()
                              if line and not line.startswith("#"))
                assert config["security.mcode"] == f"GC{letter}{number}J{spec}A"
                assert "adapter.override_ip" in config, (version, mode)
                assert len(config["eamuse.pcbid"]) == 20
                launch = package.read(f"gamestart-v{version}-{mode}.bat").decode()
                for token in (f"gdv{version}.exe -{switch}",
                              f"--config {prefix}-{mode}.conf"):
                    assert token in launch, (version, mode, token)
                lines = launch.splitlines()
                assert 'cd /d "%~dp0"' in lines, (version, mode)
                assert [line for line in lines if "mkdir" in line] == [
                    "if not exist DEV mkdir DEV",
                    f"if not exist DEV\\{mode.upper()} mkdir DEV\\{mode.upper()}",
                ], (version, mode, lines)
                assert ".xml" not in launch.lower(), (version, mode)
                assert " -I " not in launch and " -b " not in launch, (version, mode)
            print(f"PASS {prefix}: files, decoder, shared hook, GF/DM model and launcher wiring")


if __name__ == "__main__":
    check(pathlib.Path(sys.argv[1]))
