use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

fn faust_compile(input: &Path, output: &Path, class_name: Option<&str>) {
    let mut cmd = Command::new("faust");
    cmd.args(["-lang", "rust", "-i", "-I", "dsp"]);
    if let Some(cn) = class_name {
        cmd.args(["-cn", cn]);
    }
    cmd.args([
        input.to_str().unwrap(),
        "-o",
        output.to_str().unwrap(),
    ]);

    let status = cmd.status().expect("Failed to run faust");
    if !status.success() {
        panic!(
            "Faust compilation failed for {}: status {}",
            input.display(),
            status
        );
    }
}

fn main() {
    let out_dir: PathBuf = env::var_os("OUT_DIR").unwrap().into();

    // Main DSP.
    println!("cargo:rerun-if-changed=dsp/nilamp.dsp");
    println!("cargo:rerun-if-changed=dsp/hk_adnl.lib");
    println!("cargo:rerun-if-changed=dsp/hk_pkd.lib");
    println!("cargo:rerun-if-changed=dsp/hk_filters.lib");
    println!("cargo:rerun-if-changed=dsp/hk_tube.lib");
    println!("cargo:rerun-if-changed=dsp/5e3_tables.lib");

    faust_compile(
        Path::new("dsp/nilamp.dsp"),
        &out_dir.join("dsp.rs"),
        None,
    );

    // Per-stage test DSPs. Each is compiled with -cn <stem> so the generated
    // mydsp struct is renamed to <stem>, avoiding collisions when the test
    // crate includes multiple files in the same module.
    let tests_dir = Path::new("dsp/tests");
    if tests_dir.is_dir() {
        for entry in fs::read_dir(tests_dir).expect("read dsp/tests") {
            let entry = entry.expect("read dir entry");
            let path = entry.path();
            if path.extension().and_then(|e| e.to_str()) != Some("dsp") {
                continue;
            }
            let stem = path
                .file_stem()
                .and_then(|s| s.to_str())
                .expect("test dsp filename utf8");
            println!("cargo:rerun-if-changed={}", path.display());
            let out_path = out_dir.join(format!("{stem}.rs"));
            faust_compile(&path, &out_path, Some(stem));
        }
    }
}
