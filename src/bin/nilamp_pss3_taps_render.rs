// SPDX-License-Identifier: MIT
//
// Offline renderer for dsp/diagnostics/nilamp_pss3_taps.dsp.  Writes a
// 9-channel float32 WAV exposing internal taps of the corrected
// 3-stage PSS topology + pre-T4 chain pipeline.  Used to localise the
// gain deficit observed when the topology fix is enabled (see
// docs/next-session.md).
//
// Channel layout:
//
//     0. v_out          final output (sanity)
//     1. res1_v         T1 plate
//     2. res3_v         T2 plate (post-tonestack)
//     3. res4_v         T3 plate (cathodyne)
//     4. drive_t4       T4 input (after k1/hp3/peq/hs chain)
//     5. res5_v         T4 plate
//     6. res_t5_v       T5 plate
//     7. dvs2           B+ delta seen by power tubes
//     8. dvs3           B+ delta seen by preamp tubes

use std::env;
use std::path::PathBuf;
use std::process;

use nilamp::faust::{FaustDsp, ParamIndex};

mod diag {
    #![allow(
        non_snake_case,
        non_camel_case_types,
        non_upper_case_globals,
        dead_code,
        unused_mut,
        unused_variables,
        unused_parens,
        clippy::all
    )]
    use nilamp::faust::*;
    include!(concat!(env!("OUT_DIR"), "/nilamp_pss3_taps.rs"));
}

const NUM_CHANNELS: usize = 9;

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

const USAGE: &str = "\
nilamp_pss3_taps_render --input IN.wav --output OUT.wav [params]

Writes a 9-channel float32 WAV with channels:
   0. v_out          final output (sanity)
   1. res1_v         T1 plate
   2. res3_v         T2 plate (post-tonestack)
   3. res4_v         T3 plate (cathodyne)
   4. drive_t4       T4 input (after k1/hp3/peq/hs chain)
   5. res5_v         T4 plate
   6. res_t5_v       T5 plate
   7. dvs2           B+ delta seen by power tubes (T4/T5)
   8. dvs3           B+ delta seen by preamp tubes (T1/T2/T3)

Params:
  --gain    -24..24    Input gain (dB)
  --volume  0..100     Volume (%)
  --bass    0..100     Bass tone (%)
  --mid     0..100     Mid tone (%)
  --treble  0..100     Treble tone (%)
  --sag     0..100     PSS sag (%)
  --block   N>0        Faust compute() block size (default 64)
";

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
                args.gain_db = need(&mut it)?.parse().map_err(|e| format!("--gain: {e}"))?
            }
            "--volume" => {
                args.volume_pct = need(&mut it)?
                    .parse()
                    .map_err(|e| format!("--volume: {e}"))?
            }
            "--bass" => {
                args.bass_pct = need(&mut it)?.parse().map_err(|e| format!("--bass: {e}"))?
            }
            "--mid" => args.mid_pct = need(&mut it)?.parse().map_err(|e| format!("--mid: {e}"))?,
            "--treble" => {
                args.treble_pct = need(&mut it)?
                    .parse()
                    .map_err(|e| format!("--treble: {e}"))?
            }
            "--sag" => args.sag_pct = need(&mut it)?.parse().map_err(|e| format!("--sag: {e}"))?,
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

    let mut dsp = diag::nilamp_pss3_taps::new();
    diag::nilamp_pss3_taps::class_init(sample_rate);
    dsp.init(sample_rate);
    dsp.instance_clear();

    let n_inputs = dsp.get_num_inputs() as usize;
    let n_outputs = dsp.get_num_outputs() as usize;
    if n_inputs != 1 || n_outputs != NUM_CHANNELS {
        return Err(format!(
            "expected 1-in / {NUM_CHANNELS}-out diagnostic DSP, got {n_inputs}-in / {n_outputs}-out"
        ));
    }

    dsp.set_param(ParamIndex(0), args.bass_pct);
    dsp.set_param(ParamIndex(1), args.gain_db);
    dsp.set_param(ParamIndex(2), args.mid_pct);
    dsp.set_param(ParamIndex(3), args.sag_pct);
    dsp.set_param(ParamIndex(4), args.treble_pct);
    dsp.set_param(ParamIndex(5), args.volume_pct);

    let block = args.block;
    let mut out_channels: Vec<Vec<f32>> =
        (0..NUM_CHANNELS).map(|_| vec![0.0f32; total_frames]).collect();
    let mut out_scratch: Vec<Vec<f32>> = (0..NUM_CHANNELS).map(|_| vec![0.0f32; block]).collect();
    let mut pos = 0;
    while pos < total_frames {
        let n = block.min(total_frames - pos);
        let in_slice: &[f32] = &mono_in[pos..pos + n];
        for buf in &mut out_scratch {
            for s in &mut buf[..n] {
                *s = 0.0;
            }
        }
        let inputs: [&[f32]; 1] = [in_slice];
        {
            let mut output_views: Vec<&mut [f32]> =
                out_scratch.iter_mut().map(|b| &mut b[..n]).collect();
            dsp.compute(n, &inputs, &mut output_views);
        }
        for ch in 0..NUM_CHANNELS {
            out_channels[ch][pos..pos + n].copy_from_slice(&out_scratch[ch][..n]);
        }
        pos += n;
    }

    let out_spec = hound::WavSpec {
        channels: NUM_CHANNELS as u16,
        sample_rate: spec.sample_rate,
        bits_per_sample: 32,
        sample_format: hound::SampleFormat::Float,
    };
    let mut writer = hound::WavWriter::create(&args.output, out_spec)
        .map_err(|e| format!("creating {}: {e}", args.output.display()))?;
    for f in 0..total_frames {
        for ch in 0..NUM_CHANNELS {
            writer
                .write_sample(out_channels[ch][f])
                .map_err(|e| format!("writing sample: {e}"))?;
        }
    }
    writer
        .finalize()
        .map_err(|e| format!("finalising wav: {e}"))?;

    eprintln!(
        "rendered {} frames @ {} Hz, {} channels -> {}",
        total_frames,
        spec.sample_rate,
        NUM_CHANNELS,
        args.output.display()
    );
    Ok(())
}
