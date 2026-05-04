use std::process::Command;
use std::env;
use std::path::Path;

fn main() {
    let out_dir = env::var_os("OUT_DIR").unwrap();
    let dest_path = Path::new(&out_dir).join("dsp.rs");

    // Tell Cargo that if dsp/nilamp.dsp changes, it should rerun this build script.
    println!("cargo:rerun-if-changed=dsp/nilamp.dsp");

    let status = Command::new("faust")
        .args(&[
            "-lang", "rust",
            "-i", // inline library
            "dsp/nilamp.dsp",
            "-o", dest_path.to_str().unwrap(),
        ])
        .status()
        .expect("Failed to run faust");

    if !status.success() {
        panic!("Faust compilation failed with status {}", status);
    }
}
