// SPDX-License-Identifier: MIT
//
// Shared test support: Faust runtime types + minimal reflection helpers.
//
// Faust generates Rust code that references types named `F32`, `FaustFloat`,
// `Meta`, `UI`, `ParamIndex`, and (optionally) the `FaustDsp` trait.  The
// generated code does not import them; it expects them to be in module
// scope at the `include!` site.  This module provides them.

#![allow(dead_code)]

pub type FaustFloat = f32;
pub type F32 = f32;
pub type F64 = f64;

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct ParamIndex(pub usize);

pub trait Meta {
    fn declare(&mut self, key: &str, value: &str);
}

pub trait UI<T> {
    fn open_tab_box(&mut self, label: &str);
    fn open_horizontal_box(&mut self, label: &str);
    fn open_vertical_box(&mut self, label: &str);
    fn close_box(&mut self);
    fn add_button(&mut self, label: &str, param: ParamIndex);
    fn add_check_button(&mut self, label: &str, param: ParamIndex);
    fn add_vertical_slider(
        &mut self,
        label: &str,
        param: ParamIndex,
        init: T,
        min: T,
        max: T,
        step: T,
    );
    fn add_horizontal_slider(
        &mut self,
        label: &str,
        param: ParamIndex,
        init: T,
        min: T,
        max: T,
        step: T,
    );
    fn add_num_entry(&mut self, label: &str, param: ParamIndex, init: T, min: T, max: T, step: T);
    fn add_horizontal_bargraph(&mut self, label: &str, param: ParamIndex, min: T, max: T);
    fn add_vertical_bargraph(&mut self, label: &str, param: ParamIndex, min: T, max: T);
    fn declare(&mut self, param: Option<ParamIndex>, key: &str, value: &str);
}

pub trait FaustDsp {
    type T;
    fn new() -> Self
    where
        Self: Sized;
    fn metadata(&self, m: &mut dyn Meta);
    fn get_sample_rate(&self) -> i32;
    fn get_num_inputs(&self) -> i32;
    fn get_num_outputs(&self) -> i32;
    fn class_init(sample_rate: i32)
    where
        Self: Sized;
    fn instance_reset_params(&mut self);
    fn instance_clear(&mut self);
    fn instance_constants(&mut self, sample_rate: i32);
    fn instance_init(&mut self, sample_rate: i32);
    fn init(&mut self, sample_rate: i32);
    fn build_user_interface(&self, ui_interface: &mut dyn UI<Self::T>);
    fn build_user_interface_static(ui_interface: &mut dyn UI<Self::T>)
    where
        Self: Sized;
    fn get_param(&self, param: ParamIndex) -> Option<Self::T>;
    fn set_param(&mut self, param: ParamIndex, value: Self::T);
    fn compute(&mut self, count: i32, inputs: &[&[Self::T]], outputs: &mut [&mut [Self::T]]);
}

// ---------------------------------------------------------------------------
// Param-name reflection
// ---------------------------------------------------------------------------

/// Captures (label, ParamIndex) pairs from a Faust DSP's static UI walk.
/// Use `from_walk` with the generated `build_user_interface_static` function.
#[derive(Default)]
pub struct ParamMap {
    pairs: Vec<(String, ParamIndex)>,
}

impl ParamMap {
    pub fn lookup(&self, name: &str) -> ParamIndex {
        self.pairs
            .iter()
            .find(|(n, _)| n == name)
            .map(|(_, idx)| *idx)
            .unwrap_or_else(|| panic!("parameter '{name}' not found in DSP UI"))
    }

    pub fn names(&self) -> impl Iterator<Item = &str> {
        self.pairs.iter().map(|(n, _)| n.as_str())
    }
}

impl Meta for ParamMap {
    fn declare(&mut self, _: &str, _: &str) {}
}

impl<T: Copy + Default> UI<T> for ParamMap {
    fn open_tab_box(&mut self, _: &str) {}
    fn open_horizontal_box(&mut self, _: &str) {}
    fn open_vertical_box(&mut self, _: &str) {}
    fn close_box(&mut self) {}
    fn add_button(&mut self, label: &str, param: ParamIndex) {
        self.pairs.push((label.to_string(), param));
    }
    fn add_check_button(&mut self, label: &str, param: ParamIndex) {
        self.pairs.push((label.to_string(), param));
    }
    fn add_vertical_slider(&mut self, label: &str, param: ParamIndex, _: T, _: T, _: T, _: T) {
        self.pairs.push((label.to_string(), param));
    }
    fn add_horizontal_slider(&mut self, label: &str, param: ParamIndex, _: T, _: T, _: T, _: T) {
        self.pairs.push((label.to_string(), param));
    }
    fn add_num_entry(&mut self, label: &str, param: ParamIndex, _: T, _: T, _: T, _: T) {
        self.pairs.push((label.to_string(), param));
    }
    fn add_horizontal_bargraph(&mut self, _: &str, _: ParamIndex, _: T, _: T) {}
    fn add_vertical_bargraph(&mut self, _: &str, _: ParamIndex, _: T, _: T) {}
    fn declare(&mut self, _: Option<ParamIndex>, _: &str, _: &str) {}
}

// ---------------------------------------------------------------------------
// Fixture I/O — read/write float32 little-endian binary buffers.
// ---------------------------------------------------------------------------

use std::fs::File;
use std::io::{Read, Write};
use std::path::Path;

pub fn read_f32_bin(path: impl AsRef<Path>) -> Vec<f32> {
    let path = path.as_ref();
    let mut f = File::open(path).unwrap_or_else(|e| panic!("open fixture {}: {e}", path.display()));
    let mut bytes = Vec::new();
    f.read_to_end(&mut bytes).expect("read fixture");
    assert!(
        bytes.len() % 4 == 0,
        "fixture {} not a multiple of 4 bytes",
        path.display()
    );
    bytes
        .chunks_exact(4)
        .map(|c| f32::from_le_bytes([c[0], c[1], c[2], c[3]]))
        .collect()
}

#[allow(dead_code)]
pub fn write_f32_bin(path: impl AsRef<Path>, data: &[f32]) {
    let mut f = File::create(path).expect("create fixture");
    for v in data {
        f.write_all(&v.to_le_bytes()).expect("write fixture");
    }
}

// ---------------------------------------------------------------------------
// Comparison helpers
// ---------------------------------------------------------------------------

pub struct CompareReport {
    pub max_abs: f32,
    pub rms: f32,
    pub n: usize,
}

pub fn compare(actual: &[f32], expected: &[f32]) -> CompareReport {
    assert_eq!(
        actual.len(),
        expected.len(),
        "length mismatch: actual={}, expected={}",
        actual.len(),
        expected.len()
    );
    let n = actual.len();
    let mut max_abs = 0f32;
    let mut sum_sq = 0f64;
    for (a, b) in actual.iter().zip(expected.iter()) {
        let d = (a - b).abs();
        if d > max_abs {
            max_abs = d;
        }
        sum_sq += (d as f64) * (d as f64);
    }
    let rms = ((sum_sq / n as f64).sqrt()) as f32;
    CompareReport { max_abs, rms, n }
}

impl CompareReport {
    pub fn assert_within(&self, max_abs: f32, max_rms: f32, label: &str) {
        if self.max_abs > max_abs || self.rms > max_rms {
            panic!(
                "{label}: tolerance exceeded — max_abs={:.3e} (limit {:.3e}), \
                 rms={:.3e} (limit {:.3e}), n={}",
                self.max_abs, max_abs, self.rms, max_rms, self.n
            );
        }
    }
}

// ---------------------------------------------------------------------------
// Block-process helper
// ---------------------------------------------------------------------------

/// Run a single-input, single-output Faust DSP over `input` and return the
/// produced output buffer.  Saves callers from re-typing the slice-of-slice
/// dance for every regression test.
pub fn run_siso<D: FaustDsp<T = f32>>(dsp: &mut D, input: &[f32]) -> Vec<f32> {
    let n = input.len();
    let mut output = vec![0f32; n];
    {
        let inputs: [&[f32]; 1] = [input];
        let mut outputs: [&mut [f32]; 1] = [&mut output];
        dsp.compute(n as i32, &inputs, &mut outputs);
    }
    output
}

/// Run a single-input, N-output Faust DSP over `input` and return all N
/// output buffers.  N is taken from the DSP at runtime via
/// `get_num_outputs()`; the caller asserts the expected count if they want
/// to.  Used by the composite tube tests where one harness emits multiple
/// pinned signals (e.g. tube_ck -> v_out + dia).
pub fn run_simo<D: FaustDsp<T = f32>>(dsp: &mut D, input: &[f32]) -> Vec<Vec<f32>> {
    let n = input.len();
    let n_out = dsp.get_num_outputs() as usize;
    let mut outputs: Vec<Vec<f32>> = (0..n_out).map(|_| vec![0f32; n]).collect();
    {
        let inputs: [&[f32]; 1] = [input];
        // Need a Vec<&mut [f32]> for the compute call.
        let mut output_refs: Vec<&mut [f32]> =
            outputs.iter_mut().map(|v| v.as_mut_slice()).collect();
        dsp.compute(n as i32, &inputs, &mut output_refs);
    }
    outputs
}

/// Run an N-input, M-output Faust DSP over equally-sized input buffers.
pub fn run_mimo<D: FaustDsp<T = f32>>(dsp: &mut D, inputs: &[&[f32]]) -> Vec<Vec<f32>> {
    assert!(!inputs.is_empty(), "run_mimo requires at least one input");
    let n = inputs[0].len();
    assert!(
        inputs.iter().all(|buf| buf.len() == n),
        "all run_mimo inputs must have the same length"
    );
    let n_out = dsp.get_num_outputs() as usize;
    let mut outputs: Vec<Vec<f32>> = (0..n_out).map(|_| vec![0f32; n]).collect();
    {
        let mut output_refs: Vec<&mut [f32]> =
            outputs.iter_mut().map(|v| v.as_mut_slice()).collect();
        dsp.compute(n as i32, inputs, &mut output_refs);
    }
    outputs
}
