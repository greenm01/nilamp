// SPDX-License-Identifier: MIT

pub mod faust;

// The plugin entry-points depend on the generated `faust::mydsp` struct, which
// is only emitted when `dsp/nilamp.dsp` compiles successfully.  build.rs sets
// the `nilamp_toplevel` cfg flag in that case (enabled by default; opt out
// with `NILAMP_BUILD_TOPLEVEL=0` if Faust ever regresses on the file).  When
// the cfg is unset, the regression tests in `tests/` still build because they
// target only the per-stage DSPs in `dsp/tests/`.
#[cfg(nilamp_toplevel)]
mod plugin {
    use crate::faust;
    use nih_plug::prelude::*;
    use std::sync::Arc;

    pub struct Nilamp {
        params: Arc<NilampParams>,
        dsp: faust::mydsp,
        // Per-channel scratch input buffers.  nih-plug `Block` is in-place
        // (input and output share the same slice), but Faust's compute()
        // demands disjoint input/output views, so we snapshot the host
        // samples into `input_scratch` before each compute() call.  Sized
        // to the maximum (channel, block) the plugin can ever see and
        // pre-allocated in `initialize()` so the audio thread never
        // allocates.
        input_scratch: Vec<Vec<f32>>,
    }

    #[derive(Params)]
    struct NilampParams {
        #[id = "gain"]
        pub gain: FloatParam,
        #[id = "volume"]
        pub volume: FloatParam,
        #[id = "bass"]
        pub bass: FloatParam,
        #[id = "mid"]
        pub mid: FloatParam,
        #[id = "treble"]
        pub treble: FloatParam,
        #[id = "sag"]
        pub sag: FloatParam,
    }

    impl Default for Nilamp {
        fn default() -> Self {
            Self {
                params: Arc::new(NilampParams::default()),
                dsp: faust::mydsp::new(),
                input_scratch: Vec::new(),
            }
        }
    }

    impl Default for NilampParams {
        fn default() -> Self {
            Self {
                gain: FloatParam::new(
                    "Input Gain",
                    0.0,
                    FloatRange::Linear {
                        min: -12.0,
                        max: 12.0,
                    },
                )
                .with_unit(" dB"),
                volume: FloatParam::new(
                    "Volume",
                    50.0,
                    FloatRange::Linear {
                        min: 0.0,
                        max: 100.0,
                    },
                )
                .with_unit(" %"),
                bass: FloatParam::new(
                    "Bass",
                    50.0,
                    FloatRange::Linear {
                        min: 0.0,
                        max: 100.0,
                    },
                )
                .with_unit(" %"),
                mid: FloatParam::new(
                    "Mid",
                    50.0,
                    FloatRange::Linear {
                        min: 0.0,
                        max: 100.0,
                    },
                )
                .with_unit(" %"),
                treble: FloatParam::new(
                    "Treble",
                    50.0,
                    FloatRange::Linear {
                        min: 0.0,
                        max: 100.0,
                    },
                )
                .with_unit(" %"),
                sag: FloatParam::new(
                    "Sag",
                    50.0,
                    FloatRange::Linear {
                        min: 0.0,
                        max: 100.0,
                    },
                )
                .with_unit(" %"),
            }
        }
    }

    impl Plugin for Nilamp {
        const NAME: &'static str = "nilamp";
        const VENDOR: &'static str = "Mason Austin Green";
        const URL: &'static str = "https://github.com/greenm01/nilamp";
        const EMAIL: &'static str = "mason@greenm01.net";

        const VERSION: &'static str = env!("CARGO_PKG_VERSION");

        const AUDIO_IO_LAYOUTS: &'static [AudioIOLayout] = &[
            AudioIOLayout {
                main_input_channels: std::num::NonZeroU32::new(1),
                main_output_channels: std::num::NonZeroU32::new(1),
                ..AudioIOLayout::const_default()
            },
            AudioIOLayout {
                main_input_channels: std::num::NonZeroU32::new(2),
                main_output_channels: std::num::NonZeroU32::new(2),
                ..AudioIOLayout::const_default()
            },
        ];

        const SAMPLE_ACCURATE_AUTOMATION: bool = true;

        type SysExMessage = ();
        type BackgroundTask = ();

        fn params(&self) -> Arc<dyn Params> {
            self.params.clone()
        }

        fn initialize(
            &mut self,
            audio_io_layout: &AudioIOLayout,
            buffer_config: &BufferConfig,
            _context: &mut impl InitContext<Self>,
        ) -> bool {
            self.dsp.init(buffer_config.sample_rate as i32);
            // Pre-allocate the input scratch buffers up to the worst case
            // we might see at runtime: max output channels × max block
            // size.  Sized once here so process() never has to allocate.
            let n_channels = audio_io_layout
                .main_output_channels
                .map(|n| n.get() as usize)
                .unwrap_or(0);
            let block = buffer_config.max_buffer_size as usize;
            self.input_scratch = (0..n_channels).map(|_| vec![0.0; block]).collect();
            true
        }

        fn reset(&mut self) {
            self.dsp.instance_clear();
        }

        fn process(
            &mut self,
            buffer: &mut Buffer,
            _aux: &mut AuxiliaryBuffers,
            _context: &mut impl ProcessContext<Self>,
        ) -> ProcessStatus {
            for (_offset, block) in buffer.iter_blocks(MAX_BLOCK_SIZE) {
                // Update DSP parameters from plugin parameters
                // Alphabetical order: bass(0), gain(1), mid(2), sag(3), treble(4), volume(5)
                self.dsp
                    .set_param(faust::ParamIndex(0), self.params.bass.value());
                self.dsp
                    .set_param(faust::ParamIndex(1), self.params.gain.value());
                self.dsp
                    .set_param(faust::ParamIndex(2), self.params.mid.value());
                self.dsp
                    .set_param(faust::ParamIndex(3), self.params.sag.value());
                self.dsp
                    .set_param(faust::ParamIndex(4), self.params.treble.value());
                self.dsp
                    .set_param(faust::ParamIndex(5), self.params.volume.value());

                let samples = block.samples();

                // nih-plug `Block` is in-place: each channel slice is *both*
                // input and output to the host.  Faust's compute() takes
                // disjoint `&[&[f32]]` / `&mut [&mut [f32]]`, so we snapshot
                // the host samples into the pre-allocated `input_scratch`
                // (sized in `initialize()`) and feed Faust two disjoint
                // borrows.
                let mut output_slices: Vec<&mut [f32]> = block.into_iter().collect();
                let n_channels = output_slices.len();
                if n_channels == 0 {
                    continue;
                }
                debug_assert!(
                    n_channels <= self.input_scratch.len(),
                    "process() saw more channels than initialize() reserved",
                );
                debug_assert!(
                    self.input_scratch
                        .first()
                        .is_none_or(|b| samples <= b.len()),
                    "process() saw a larger block than initialize() reserved",
                );

                // Snapshot host input into scratch[..n_channels][..samples].
                for (ch_idx, out) in output_slices.iter().enumerate() {
                    self.input_scratch[ch_idx][..samples].copy_from_slice(&out[..samples]);
                }

                // Build the disjoint views Faust expects.  `input_views`
                // borrows from `self.input_scratch`; `output_views` borrows
                // from the host buffer via `output_slices`.  No aliasing,
                // no unsafe.
                let input_views: Vec<&[f32]> = self
                    .input_scratch
                    .iter()
                    .take(n_channels)
                    .map(|buf| &buf[..samples])
                    .collect();
                let mut output_views: Vec<&mut [f32]> = output_slices
                    .iter_mut()
                    .map(|out| &mut out[..samples])
                    .collect();

                self.dsp.compute(samples, &input_views, &mut output_views);
            }

            ProcessStatus::Normal
        }
    }

    impl ClapPlugin for Nilamp {
        const CLAP_ID: &'static str = "net.greenm01.nilamp";
        const CLAP_DESCRIPTION: Option<&'static str> =
            Some("A Linux-native CLAP guitar amp plugin");
        const CLAP_MANUAL_URL: Option<&'static str> = Some(Self::URL);
        const CLAP_SUPPORT_URL: Option<&'static str> = None;
        const CLAP_FEATURES: &'static [ClapFeature] = &[
            ClapFeature::AudioEffect,
            ClapFeature::Stereo,
            ClapFeature::Distortion,
        ];
    }

    impl Vst3Plugin for Nilamp {
        const VST3_CLASS_ID: [u8; 16] = *b"nilamp-vst3-dist";
        const VST3_SUBCATEGORIES: &'static [Vst3SubCategory] =
            &[Vst3SubCategory::Fx, Vst3SubCategory::Distortion];
    }

    nih_export_clap!(crate::plugin::Nilamp);
    nih_export_vst3!(crate::plugin::Nilamp);

    const MAX_BLOCK_SIZE: usize = 64;
} // mod plugin

#[cfg(nilamp_toplevel)]
pub use plugin::Nilamp;
