// SPDX-License-Identifier: MIT
//
// nilamp_render: offline batch renderer that drives the same Faust DSP
// the plugin uses, for ABX harness work and bench A/B comparisons.
//
// Usage:
//   nilamp_render --input in.wav --output out.wav \
//                 [--gain 0] [--volume 50] [--bass 50] [--mid 50] \
//                 [--treble 50] [--sag 50] [--block 64]
//
// Param ranges match the plugin's NilampParams (see src/lib.rs):
//   gain    -24..24   (dB; 0 = unity)
//   volume  0..100    (%, mapped to 0..1 in Faust)
//   bass    0..100
//   mid     0..100
//   treble  0..100
//   sag     0..100
//
// Block size mirrors the plugin's MAX_BLOCK_SIZE default (64); pass
// --block to vary it for stress testing.

use std::env;
use std::path::PathBuf;
use std::process;

use nilamp::faust::{self, FaustDsp};

#[derive(Debug)]
struct Args {
    input: PathBuf,
    output: PathBuf,
    gain_db: f32,
    volume_pct: f32,
    bass_pct: f32,
    mid_pct: f32,
    treble_pct: f32,
    sag_pct: f32,
    block: usize,
}

impl Default for Args {
    fn default() -> Self {
        Self {
            input: PathBuf::new(),
            output: PathBuf::new(),
            gain_db: 0.0,
            volume_pct: 50.0,
            bass_pct: 50.0,
            mid_pct: 50.0,
            treble_pct: 50.0,
            sag_pct: 50.0,
            block: 64,
        }
    }
}

fn parse_args() -> Result<Args, String> {
    let mut args = Args::default();
    let mut it = env::args().skip(1);
    while let Some(arg) = it.next() {
        let need = |it: &mut std::iter::Skip<env::Args>| -> Result<String, String> {
            it.next().ok_or_else(|| format!("missing value for {arg}"))
        };
        match arg.as_str() {
            "--input" => args.input = PathBuf::from(need(&mut it)?),
            "--output" => args.output = PathBuf::from(need(&mut it)?),
            "--gain" => {
                args.gain_db = need(&mut it)?
                    .parse()
                    .map_err(|e| format!("--gain: {e}"))?
            }
            "--volume" => {
                args.volume_pct = need(&mut it)?
                    .parse()
                    .map_err(|e| format!("--volume: {e}"))?
            }
            "--bass" => {
                args.bass_pct = need(&mut it)?
                    .parse()
                    .map_err(|e| format!("--bass: {e}"))?
            }
            "--mid" => {
                args.mid_pct = need(&mut it)?
                    .parse()
                    .map_err(|e| format!("--mid: {e}"))?
            }
            "--treble" => {
                args.treble_pct = need(&mut it)?
                    .parse()
                    .map_err(|e| format!("--treble: {e}"))?
            }
            "--sag" => {
                args.sag_pct = need(&mut it)?
                    .parse()
                    .map_err(|e| format!("--sag: {e}"))?
            }
            "--block" => {
                args.block = need(&mut it)?
                    .parse()
                    .map_err(|e| format!("--block: {e}"))?
            }
            "-h" | "--help" => {
                eprintln!("{}", USAGE);
                process::exit(0);
            }
            _ => return Err(format!("unknown argument: {arg}")),
        }
    }
    if args.input.as_os_str().is_empty() {
        return Err("--input is required".into());
    }
    if args.output.as_os_str().is_empty() {
        return Err("--output is required".into());
    }
    if args.block == 0 {
        return Err("--block must be > 0".into());
    }
    Ok(args)
}

const USAGE: &str = "\
nilamp_render --input IN.wav --output OUT.wav [params]

Params (defaults match plugin defaults; ranges match NilampParams):
  --gain    -24..24    Input gain (dB)
  --volume  0..100     Volume (%)
  --bass    0..100     Bass tone (%)
  --mid     0..100     Mid tone (%)
  --treble  0..100     Treble tone (%)
  --sag     0..100     PSS sag (%)
  --block   N>0        Faust compute() block size (default 64)
";

fn main() {
    let args = match parse_args() {
        Ok(a) => a,
        Err(e) => {
            eprintln!("error: {e}\n\n{USAGE}");
            process::exit(2);
        }
    };

    if let Err(e) = run(&args) {
        eprintln!("error: {e}");
        process::exit(1);
    }
}

fn run(args: &Args) -> Result<(), String> {
    // --- Read input WAV ---
    let mut reader = hound::WavReader::open(&args.input)
        .map_err(|e| format!("opening {}: {e}", args.input.display()))?;
    let spec = reader.spec();
    let sample_rate = spec.sample_rate as i32;
    let in_channels = spec.channels as usize;

    let samples: Vec<f32> = match (spec.sample_format, spec.bits_per_sample) {
        (hound::SampleFormat::Float, 32) => reader
            .samples::<f32>()
            .collect::<Result<Vec<_>, _>>()
            .map_err(|e| format!("reading float samples: {e}"))?,
        (hound::SampleFormat::Int, bits) => {
            let scale = 1.0_f32 / ((1_i64 << (bits - 1)) as f32);
            reader
                .samples::<i32>()
                .collect::<Result<Vec<_>, _>>()
                .map_err(|e| format!("reading int samples: {e}"))?
                .into_iter()
                .map(|s| s as f32 * scale)
                .collect()
        }
        (fmt, bits) => return Err(format!("unsupported wav format {fmt:?} {bits}-bit")),
    };

    // De-interleave to a mono input buffer (collapsing stereo input by averaging,
// since the Faust DSP is mono-in per the .dsp port). Output channel count is
// taken from the DSP itself (currently 1-in / 2-out: mono input, stereo output).
    let total_frames = samples.len() / in_channels;
    let mut mono_in: Vec<f32> = Vec::with_capacity(total_frames);
    if in_channels == 1 {
        mono_in.extend_from_slice(&samples);
    } else {
        for f in 0..total_frames {
            let mut acc = 0.0f32;
            for c in 0..in_channels {
                acc += samples[f * in_channels + c];
            }
            mono_in.push(acc / in_channels as f32);
        }
    }

    // --- Init Faust DSP ---
    let mut dsp = faust::mydsp::new();
    faust::mydsp::class_init(sample_rate);
    dsp.init(sample_rate);
    dsp.instance_clear();

    let n_inputs = dsp.get_num_inputs() as usize;
    let n_outputs = dsp.get_num_outputs() as usize;
    if n_inputs != 1 || n_outputs == 0 {
        return Err(format!(
            "expected 1-in / N-out Faust DSP (N>=1), got {n_inputs}-in / {n_outputs}-out"
        ));
    }

    // Param indices match src/lib.rs alphabetical order:
    //   bass(0), gain(1), mid(2), sag(3), treble(4), volume(5).
    // Plugin params: gain in dB, others in 0..100; the .dsp converts gain to
    // linear via ba.db2linear and 0..100 to 0..1 via /100.0, so we forward
    // the user-facing values directly.
    dsp.set_param(faust::ParamIndex(0), args.bass_pct);
    dsp.set_param(faust::ParamIndex(1), args.gain_db);
    dsp.set_param(faust::ParamIndex(2), args.mid_pct);
    dsp.set_param(faust::ParamIndex(3), args.sag_pct);
    dsp.set_param(faust::ParamIndex(4), args.treble_pct);
    dsp.set_param(faust::ParamIndex(5), args.volume_pct);

    // --- Process ---
    // Output is interleaved: total_frames * n_outputs.
    let mut output: Vec<f32> = vec![0.0; total_frames * n_outputs];
    let block = args.block;
    // Pre-allocate per-channel scratch outside the hot loop.
    let mut out_scratch: Vec<Vec<f32>> = (0..n_outputs).map(|_| vec![0.0f32; block]).collect();
    let mut pos = 0;
    while pos < total_frames {
        let n = block.min(total_frames - pos);
        let in_slice: &[f32] = &mono_in[pos..pos + n];
        for buf in &mut out_scratch {
            // Only the first n samples will be written; zero defensively.
            for s in &mut buf[..n] {
                *s = 0.0;
            }
        }
        let inputs: [&[f32]; 1] = [in_slice];
        {
            let mut output_views: Vec<&mut [f32]> = out_scratch
                .iter_mut()
                .map(|b| &mut b[..n])
                .collect();
            dsp.compute(n, &inputs, &mut output_views);
        }
        // Interleave into output buffer.
        for f in 0..n {
            for c in 0..n_outputs {
                output[(pos + f) * n_outputs + c] = out_scratch[c][f];
            }
        }
        pos += n;
    }

    // --- Write output WAV (32-bit float, n_outputs channels, same sample rate) ---
    let out_spec = hound::WavSpec {
        channels: n_outputs as u16,
        sample_rate: spec.sample_rate,
        bits_per_sample: 32,
        sample_format: hound::SampleFormat::Float,
    };
    let mut writer = hound::WavWriter::create(&args.output, out_spec)
        .map_err(|e| format!("creating {}: {e}", args.output.display()))?;
    for &s in &output {
        writer
            .write_sample(s)
            .map_err(|e| format!("writing sample: {e}"))?;
    }
    writer
        .finalize()
        .map_err(|e| format!("finalising wav: {e}"))?;

    eprintln!(
        "rendered {} frames @ {} Hz, {} ch -> {}",
        total_frames,
        spec.sample_rate,
        n_outputs,
        args.output.display()
    );
    Ok(())
}
