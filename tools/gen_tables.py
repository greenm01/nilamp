import numpy as np
import os

def glf(x, k0, b, type_b):
    """Generalized Logistic Function as implemented by Helmut Keller."""
    if type_b == 0:  # Type A
        va = np.log(k0) / np.log(1 + np.exp(b))
        kaa = -1 / va * (1 + np.exp(b))**(1 - va) / np.exp(b)
        return (1 + np.exp(b - kaa * x))**va - k0
    else:  # Type B
        vb = np.log(1 - k0) / np.log(1 + np.exp(-b))
        kab = -1 / vb * (1 + np.exp(-b))**(1 - vb) / np.exp(-b)
        return 1 - (1 + np.exp(-b + kab * x))**vb - k0

def gen_adnl_table(k0, b, type_b, kloop, xmax=15.0, dx=0.02):
    """Generate ADNL table coefficients (cubic for f, 4th order for F)."""
    
    # 1. Generate high-resolution grid for resampling if kloop > 0
    # Keller uses dx1 = dx / 3 for the internal grid
    dx1 = dx / 3.0
    x_internal = np.arange(-xmax, xmax + dx, dx1)
    f_internal = glf(x_internal, k0, b, type_b)
    
    if kloop > 0:
        # x_external_normalized = (x_internal + kloop * f_internal) / (kloop + 1)
        x_ext_norm = (x_internal + kloop * f_internal) / (kloop + 1.0)
        
        # Resample f_internal onto the uniform grid of x_ext_norm
        x_target = np.arange(-xmax, xmax + dx, dx1)
        # Interpolate f_internal(x_internal) to f_closed(x_target)
        # where x_target corresponds to x_ext_norm
        f_closed = np.interp(x_target, x_ext_norm, f_internal, left=-k0, right=1.0-k0)
    else:
        f_closed = f_internal

    # 2. Generate polynomial coefficients for each segment of size dx
    # f(x) = a3*w^3 + a2*w^2 + a1*w + a0  (where w = x - x_segment_start)
    # F(x) = b4*w^4 + b3*w^3 + b2*w^2 + b1*w + b0
    
    num_segments = int(2 * xmax / dx)
    table = []
    
    f0 = f_closed[0]
    b00 = 0.0
    b10 = 0.0
    b20 = 0.0
    b30 = 0.0
    b40 = 0.0
    
    for i in range(num_segments):
        # Index in f_closed corresponds to dx1 = dx/3
        idx = i * 3
        # Values at 0, dx/3, 2dx/3, dx
        v0 = f_closed[idx]
        v1 = f_closed[idx+1]
        v2 = f_closed[idx+2]
        v3 = f_closed[idx+3]
        
        # Cubic fit for f(x) in this segment [0, dx]
        # f(w) = a3*w^3 + a2*w^2 + a1*w + a0
        a0 = v0
        # Solving for a1, a2, a3 using v1, v2, v3
        # v1 = a3*(dx/3)^3 + a2*(dx/3)^2 + a1*(dx/3) + a0
        # ...
        # Keller's formulas:
        # a1 = (v3 - 4.5*v2 + 9*v1 - 5.5*v0) / dx  -- Wait, Keller has different formulas
        # a1 = (f3 - 4.5*f2 + 9*f1) / dx -- No, wait, he subtracts a0 from f1,f2,f3 first
        
        f1_rel = v1 - a0
        f2_rel = v2 - a0
        f3_rel = v3 - a0
        
        a1 = (f3_rel - 4.5*f2_rel + 9.0*f1_rel) / dx
        a2 = (-4.5*f3_rel + 18.0*f2_rel - 22.5*f1_rel) / (dx**2)
        a3 = (4.5*f3_rel - 13.5*f2_rel + 13.5*f1_rel) / (dx**3)
        
        # Antiderivative F(x) = integral f(x)
        # F(w) = (a3/4)*w^4 + (a2/3)*w^3 + (a1/2)*w^2 + a0*w + C
        # b4 = a3/4, b3 = a2/3, b2 = a1/2, b1 = a0
        b4 = a3 / 4.0
        b3 = a2 / 3.0
        b2 = a1 / 2.0
        b1 = a0
        
        # C is chosen such that F is continuous at segment boundaries
        # b0 = F_prev(dx)
        b0 = b00 + dx * (b10 + dx * (b20 + dx * (b30 + dx * b40)))
        
        # Store coefficients in Keller's order:
        # ab[0]=a3, ab[1]=a2, ab[2]=a1, ab[3]=a0, ab[4]=b4, ab[5]=b3, ab[6]=b2, ab[7]=b1, ab[8]=b0
        table.append([a3, a2, a1, a0, b4, b3, b2, b1, b0])
        
        b00, b10, b20, b30, b40 = b0, b1, b2, b3, b4

    return np.array(table)

def export_faust_table(name, table):
    """Export table to a Faust-compatible library string."""
    s = f"{name} = waveform {{"
    flat_table = table.flatten()
    s += ", ".join([f"{v:.10e}" for v in flat_table])
    s += "};\n"
    return s

if __name__ == "__main__":
    # Test generation for 12AX7 common cathode stage
    # k0 = ibias / isat
    k0 = 0.00076 / 0.00165
    b = 0.0
    type_b = 0
    kloop = 0.0
    
    table = gen_adnl_table(k0, b, type_b, kloop)
    print(f"Generated table with {len(table)} segments")
    # print(export_faust_table("tube1_table", table)[:200])
