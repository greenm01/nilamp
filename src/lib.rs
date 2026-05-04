pub mod faust;

use nih_plug::prelude::*;
use std::sync::Arc;

pub struct Nilamp {
    params: Arc<NilampParams>,
    dsp: faust::mydsp,
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
        _audio_io_layout: &AudioIOLayout,
        buffer_config: &BufferConfig,
        _context: &mut impl InitContext<Self>,
    ) -> bool {
        self.dsp.init(buffer_config.sample_rate as i32);
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
        for (_offset, mut block) in buffer.iter_blocks(MAX_BLOCK_SIZE) {
            // Update DSP parameters from plugin parameters
            // Alphabetical order: bass(0), gain(1), mid(2), sag(3), treble(4), volume(5)
            self.dsp.set_param(faust::ParamIndex(0), self.params.bass.value());
            self.dsp.set_param(faust::ParamIndex(1), self.params.gain.value());
            self.dsp.set_param(faust::ParamIndex(2), self.params.mid.value());
            self.dsp.set_param(faust::ParamIndex(3), self.params.sag.value());
            self.dsp.set_param(faust::ParamIndex(4), self.params.treble.value());
            self.dsp.set_param(faust::ParamIndex(5), self.params.volume.value());

            let samples = block.samples();
            let mut channels: Vec<*mut f32> = block.into_iter().map(|c| c.as_mut_ptr()).collect();
            
            if !channels.is_empty() {
                unsafe {
                    let input_ptrs: Vec<&[f32]> = channels.iter().map(|&ptr| std::slice::from_raw_parts(ptr, samples)).collect();
                    let mut output_ptrs: Vec<&mut [f32]> = channels.iter().map(|&ptr| std::slice::from_raw_parts_mut(ptr, samples)).collect();
                    
                    self.dsp.compute(samples, &input_ptrs, &mut output_ptrs);
                }
            }
        }

        ProcessStatus::Normal
    }
}

impl ClapPlugin for Nilamp {
    const CLAP_ID: &'static str = "net.greenm01.nilamp";
    const CLAP_DESCRIPTION: Option<&'static str> = Some("A Linux-native CLAP guitar amp plugin");
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

nih_export_clap!(Nilamp);
nih_export_vst3!(Nilamp);

const MAX_BLOCK_SIZE: usize = 64;
