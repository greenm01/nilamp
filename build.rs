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
        faust_compile_with_timeout(
            Path::new("dsp/nilamp.dsp"),
            &out_dir.join("dsp.rs"),
            None,
            Some("300"),
        );
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
    //
    // Gated behind the `dsp-tests` Cargo feature so iterative
    // `cargo build --release --bin nilamp_render` cycles don't pay the
    // cost of translating + LLVM-optimizing 18 test DSPs.  Run with
    // `cargo test --features dsp-tests` (or build with the same feature)
    // to enable.  When disabled, an empty stub is written so any
    // `include!()` referencing the file still finds a (no-op) module.
    let tests_dir = Path::new("dsp/tests");
    println!("cargo:rerun-if-changed=dsp/tests");
    println!("cargo:rerun-if-env-changed=CARGO_FEATURE_DSP_TESTS");
    let build_tests = env::var("CARGO_FEATURE_DSP_TESTS").is_ok();
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
            if build_tests {
                faust_compile(&path, &out_path, Some(stem));
            } else {
                let stub = format!(
                    "// dsp/tests/{stem}.dsp skipped: feature dsp-tests not enabled.\n"
                );
                fs::write(&out_path, stub).expect("write test dsp stub");
            }
        }
    }

    // Diagnostic DSPs (full-pipeline tap probes).  Large (~670 KB of
    // generated Rust each) and only needed by the diagnostic render
    // binaries (which declare `required-features = ["dsp-diagnostics"]`),
    // so we gate them off by default to keep the main iteration loop fast.
    let diagnostics_dir = Path::new("dsp/diagnostics");
    println!("cargo:rerun-if-changed=dsp/diagnostics");
    println!("cargo:rerun-if-env-changed=CARGO_FEATURE_DSP_DIAGNOSTICS");
    let build_diagnostics = env::var("CARGO_FEATURE_DSP_DIAGNOSTICS").is_ok();
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
            if build_diagnostics {
                faust_compile_with_timeout(&path, &out_path, Some(stem), Some("300"));
            } else {
                let stub = format!(
                    "// dsp/diagnostics/{stem}.dsp skipped: feature dsp-diagnostics not enabled.\n"
                );
                fs::write(&out_path, stub).expect("write diagnostic dsp stub");
            }
        }
    }
}
