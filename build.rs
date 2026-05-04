use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

fn faust_compile(input: &Path, output: &Path, class_name: Option<&str>) {
    faust_compile_with_timeout(input, output, class_name, None);
}

fn faust_compile_with_timeout(
    input: &Path,
    output: &Path,
    class_name: Option<&str>,
    timeout_s: Option<&str>,
) {
    let mut cmd = Command::new("faust");
    if let Some(timeout_s) = timeout_s {
        cmd.args(["-t", timeout_s]);
    }
    cmd.args(["-lang", "rust", "-i", "-I", "dsp"]);
    if let Some(cn) = class_name {
        cmd.args(["-cn", cn]);
    }
    cmd.args([input.to_str().unwrap(), "-o", output.to_str().unwrap()]);

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

    // Tell rustc about the optional cfg flag.
    println!("cargo::rustc-check-cfg=cfg(nilamp_toplevel)");

    // The top-level 5E3 amp (`dsp/nilamp.dsp`) compiles to ~500K of Rust in
    // ~30 s on Faust 2.85.x.  Earlier in the project Faust would SIGALRM on
    // this file (the 4×13503-cell waveforms + global feedback loop tripped
    // its internal timeout) and it was opt-in via `NILAMP_BUILD_TOPLEVEL=1`.
    // After the 5e3_constants.lib refactor Faust now finishes well within
    // its limits, so the top-level build is enabled by default.  The env
    // var is kept as an *opt-out* (`NILAMP_BUILD_TOPLEVEL=0`) so we can
    // skip it again if a future change resurrects the timeout.
    println!("cargo:rerun-if-env-changed=NILAMP_BUILD_TOPLEVEL");
    let toplevel = !matches!(
        env::var("NILAMP_BUILD_TOPLEVEL").as_deref(),
        Ok("0") | Ok("false") | Ok("off") | Ok("no")
    );
    if toplevel {
        faust_compile(Path::new("dsp/nilamp.dsp"), &out_dir.join("dsp.rs"), None);
        println!("cargo:rustc-cfg=nilamp_toplevel");
    } else {
        // Emit a stub so any `include!(concat!(env!("OUT_DIR"), "/dsp.rs"))`
        // downstream still finds a file.  The stub exposes nothing.
        let stub = "// dsp/nilamp.dsp skipped: NILAMP_BUILD_TOPLEVEL=0.\n";
        fs::write(out_dir.join("dsp.rs"), stub).expect("write dsp.rs stub");
    }

    // Per-stage test DSPs. Each is compiled with -cn <stem> so the generated
    // mydsp struct is renamed to <stem>, avoiding collisions when the test
    // crate includes multiple files in the same module.
    let tests_dir = Path::new("dsp/tests");
    // Ensure cargo re-runs this build script when test DSPs are added or
    // removed (a plain rerun-if-changed on each file misses additions).
    println!("cargo:rerun-if-changed=dsp/tests");
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

    let diagnostics_dir = Path::new("dsp/diagnostics");
    println!("cargo:rerun-if-changed=dsp/diagnostics");
    if diagnostics_dir.is_dir() {
        for entry in fs::read_dir(diagnostics_dir).expect("read dsp/diagnostics") {
            let entry = entry.expect("read dir entry");
            let path = entry.path();
            if path.extension().and_then(|e| e.to_str()) != Some("dsp") {
                continue;
            }
            let stem = path
                .file_stem()
                .and_then(|s| s.to_str())
                .expect("diagnostic dsp filename utf8");
            println!("cargo:rerun-if-changed={}", path.display());
            let out_path = out_dir.join(format!("{stem}.rs"));
            faust_compile_with_timeout(&path, &out_path, Some(stem), Some("300"));
        }
    }
}
