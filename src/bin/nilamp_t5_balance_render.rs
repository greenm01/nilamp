// SPDX-License-Identifier: MIT
//
// Offline renderer for dsp/diagnostics/nilamp_t5_balance.dsp.  It mirrors
// nilamp_render's WAV/parameter behavior but writes one selected diagnostic
// variant as mono output so tools/abx_compare.py can compare it directly.

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
    include!(concat!(env!("OUT_DIR"), "/nilamp_t5_balance.rs"));
}

#[derive(Copy, Clone, Debug)]
enum Variant {
    Current,
    RawT4FilteredT5,
    FilteredT4FilteredT5,
    SignAdd,
    HalfDenomControl,
    PostBackendCurrentSag,
    FullBackendCurrentSag,
    T4K1CurrentT3,
    T4Hp3CurrentT3,
    T4PeqHsCurrentT3,
    T4FullPreCurrentT3,
    Hp2T5SourceOnly,
    Hp2BothRaw,
}

impl Variant {
    fn parse(s: &str) -> Result<Self, String> {
        match s {
            "v0_current" | "current" | "0" => Ok(Self::Current),
            "v1_raw_t4_filtered_t5" | "raw_t4_filtered_t5" | "1" => Ok(Self::RawT4FilteredT5),
            "v2_filtered_t4_filtered_t5" | "filtered_t4_filtered_t5" | "2" => {
                Ok(Self::FilteredT4FilteredT5)
            }
            "v3_sign_add" | "sign_add" | "3" => Ok(Self::SignAdd),
            "v4_half_denom_control" | "half_denom_control" | "4" => Ok(Self::HalfDenomControl),
            "v5_post_backend_current_sag" | "post_backend_current_sag" | "5" => {
                Ok(Self::PostBackendCurrentSag)
            }
            "v6_full_backend_current_sag" | "full_backend_current_sag" | "6" => {
                Ok(Self::FullBackendCurrentSag)
            }
            "v7_t4_k1_current_t3" | "t4_k1_current_t3" | "7" => Ok(Self::T4K1CurrentT3),
            "v8_t4_hp3_current_t3" | "t4_hp3_current_t3" | "8" => Ok(Self::T4Hp3CurrentT3),
            "v9_t4_peq_hs_current_t3" | "t4_peq_hs_current_t3" | "9" => Ok(Self::T4PeqHsCurrentT3),
            "v10_t4_full_pre_current_t3" | "t4_full_pre_current_t3" | "10" => {
                Ok(Self::T4FullPreCurrentT3)
            }
            "v11_hp2_t5_source_only" | "hp2_t5_source_only" | "11" => Ok(Self::Hp2T5SourceOnly),
            "v12_hp2_both_raw" | "hp2_both_raw" | "12" => Ok(Self::Hp2BothRaw),
            _ => Err(format!("unknown variant: {s}")),
        }
    }

    fn output_index(self) -> usize {
        match self {
            Self::Current => 0,
            Self::RawT4FilteredT5 => 1,
            Self::FilteredT4FilteredT5 => 2,
            Self::SignAdd => 3,
            Self::HalfDenomControl => 4,
            Self::PostBackendCurrentSag => 5,
            Self::FullBackendCurrentSag => 6,
            Self::T4K1CurrentT3 => 7,
            Self::T4Hp3CurrentT3 => 8,
            Self::T4PeqHsCurrentT3 => 9,
            Self::T4FullPreCurrentT3 => 10,
            Self::Hp2T5SourceOnly => 11,
            Self::Hp2BothRaw => 12,
        }
    }

    fn name(self) -> &'static str {
        match self {
            Self::Current => "v0_current",
            Self::RawT4FilteredT5 => "v1_raw_t4_filtered_t5",
            Self::FilteredT4FilteredT5 => "v2_filtered_t4_filtered_t5",
            Self::SignAdd => "v3_sign_add",
            Self::HalfDenomControl => "v4_half_denom_control",
            Self::PostBackendCurrentSag => "v5_post_backend_current_sag",
            Self::FullBackendCurrentSag => "v6_full_backend_current_sag",
            Self::T4K1CurrentT3 => "v7_t4_k1_current_t3",
            Self::T4Hp3CurrentT3 => "v8_t4_hp3_current_t3",
            Self::T4PeqHsCurrentT3 => "v9_t4_peq_hs_current_t3",
            Self::T4FullPreCurrentT3 => "v10_t4_full_pre_current_t3",
            Self::Hp2T5SourceOnly => "v11_hp2_t5_source_only",
            Self::Hp2BothRaw => "v12_hp2_both_raw",
        }
    }
}

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
    variant: Variant,
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
            variant: Variant::Current,
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
            "--variant" => args.variant = Variant::parse(&need(&mut it)?)?,
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
nilamp_t5_balance_render --input IN.wav --output OUT.wav --variant NAME [params]

Variants:
  v0_current
  v1_raw_t4_filtered_t5
  v2_filtered_t4_filtered_t5
  v3_sign_add
  v4_half_denom_control
  v5_post_backend_current_sag
  v6_full_backend_current_sag
  v7_t4_k1_current_t3
  v8_t4_hp3_current_t3
  v9_t4_peq_hs_current_t3
  v10_t4_full_pre_current_t3
  v11_hp2_t5_source_only
  v12_hp2_both_raw

Params:
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

    let mut dsp = diag::nilamp_t5_balance::new();
    diag::nilamp_t5_balance::class_init(sample_rate);
    dsp.init(sample_rate);
    dsp.instance_clear();

    let n_inputs = dsp.get_num_inputs() as usize;
    let n_outputs = dsp.get_num_outputs() as usize;
    if n_inputs != 1 || n_outputs < 13 {
        return Err(format!(
            "expected 1-in / >=13-out diagnostic DSP, got {n_inputs}-in / {n_outputs}-out"
        ));
    }

    dsp.set_param(ParamIndex(0), args.bass_pct);
    dsp.set_param(ParamIndex(1), args.gain_db);
    dsp.set_param(ParamIndex(2), args.mid_pct);
    dsp.set_param(ParamIndex(3), args.sag_pct);
    dsp.set_param(ParamIndex(4), args.treble_pct);
    dsp.set_param(ParamIndex(5), args.volume_pct);

    let variant_idx = args.variant.output_index();
    let mut output: Vec<f32> = vec![0.0; total_frames];
    let block = args.block;
    let mut out_scratch: Vec<Vec<f32>> = (0..n_outputs).map(|_| vec![0.0f32; block]).collect();
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
        output[pos..pos + n].copy_from_slice(&out_scratch[variant_idx][..n]);
        pos += n;
    }

    let out_spec = hound::WavSpec {
        channels: 1,
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
        "rendered {} frames @ {} Hz, variant {} -> {}",
        total_frames,
        spec.sample_rate,
        args.variant.name(),
        args.output.display()
    );
    Ok(())
}
