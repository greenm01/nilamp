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

    // The top-level 5E3 amp (`dsp/nilamp.dsp`) currently exceeds Faust's
    // internal compile timeout (SIGALRM) when all four 13503-cell tube
    // waveforms are inlined together with the global feedback loop.
    // We rely on the per-stage test DSPs in `dsp/tests/` for validation
    // until step 7 (wire `CkConfig`-derived constants) is complete and
    // we revisit the top-level compile.  Set `NILAMP_BUILD_TOPLEVEL=1`
    // to attempt it anyway; the plugin entry-points in `src/lib.rs` are
    // gated behind `#[cfg(nilamp_toplevel)]`.
    println!("cargo:rerun-if-env-changed=NILAMP_BUILD_TOPLEVEL");
    if env::var_os("NILAMP_BUILD_TOPLEVEL").is_some() {
        faust_compile(Path::new("dsp/nilamp.dsp"), &out_dir.join("dsp.rs"), None);
        println!("cargo:rustc-cfg=nilamp_toplevel");
    } else {
        // Emit a stub so any `include!(concat!(env!("OUT_DIR"), "/dsp.rs"))`
        // downstream still finds a file.  The stub exposes nothing.
        let stub = "// dsp/nilamp.dsp skipped: set NILAMP_BUILD_TOPLEVEL=1 to enable.\n";
        fs::write(out_dir.join("dsp.rs"), stub).expect("write dsp.rs stub");
    }

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
