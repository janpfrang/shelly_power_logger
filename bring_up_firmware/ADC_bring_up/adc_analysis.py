#!/usr/bin/env python3
"""
ADC Bringup Analysis Tool
=========================

Parses serial output from ADC_Bringup.ino and:
  • Extracts ADC counts, calculated voltage, and timestamps
  • Reads multimeter measurements manually entered
  • Calculates optimal VDIV_RATIO
  • Plots voltage trends
  • Generates calibration report

Usage:
  python3 adc_analysis.py --serial-log <file.txt> --multimeter <voltage_v>
  
Or interactively:
  from adc_analysis import ADCAnalysis
  adc = ADCAnalysis("serial_output.txt")
  adc.calibrate_with_multimeter(8.95)
  adc.plot()
"""

import re
import sys
from pathlib import Path
from dataclasses import dataclass
from typing import List, Tuple, Optional
import statistics

try:
    import matplotlib.pyplot as plt
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False
    print("⚠️  matplotlib not available (install: pip install matplotlib)")

@dataclass
class ADCReading:
    """Single ADC measurement from serial output"""
    timestamp_ms: int
    adc_raw: int
    voltage_v: float
    status: str
    uptime_s: int
    
    def __repr__(self):
        return (f"ADCReading(t={self.timestamp_ms}ms, adc={self.adc_raw}, "
                f"v={self.voltage_v:.2f}V, status={self.status})")


class ADCAnalysis:
    """Parse and analyze ADC bringup firmware output"""
    
    def __init__(self, log_file: str):
        """Load serial log file"""
        self.log_file = Path(log_file)
        self.readings: List[ADCReading] = []
        self.current_vdiv_ratio: float = 1.0
        self.multimeter_reading: Optional[float] = None
        self.calibrated_vdiv_ratio: Optional[float] = None
        
        self._parse_log()
    
    def _parse_log(self):
        """Extract ADC readings from serial output log"""
        if not self.log_file.exists():
            raise FileNotFoundError(f"Log file not found: {self.log_file}")
        
        # Regex pattern matches:
        # [  1234] ADC=2048  V_rail=2.09 V  Status=OK   Uptime=1 s
        pattern = r'\[\s*(\d+)\]\s+ADC=(\d+)\s+V_rail=([\d.]+)\s+V\s+Status=(\w+)\s+Uptime=(\d+)\s+s'
        
        with open(self.log_file, 'r') as f:
            content = f.read()
        
        matches = re.finditer(pattern, content)
        count = 0
        
        for match in matches:
            timestamp_ms = int(match.group(1))
            adc_raw = int(match.group(2))
            voltage_v = float(match.group(3))
            status = match.group(4)
            uptime_s = int(match.group(5))
            
            reading = ADCReading(timestamp_ms, adc_raw, voltage_v, status, uptime_s)
            self.readings.append(reading)
            count += 1
        
        print(f"✓ Parsed {count} ADC readings from {self.log_file.name}")
        
        if not self.readings:
            raise ValueError("No ADC readings found in log. Check log format.")
    
    def set_current_vdiv_ratio(self, ratio: float):
        """Set the VDIV_RATIO that was used during logging"""
        self.current_vdiv_ratio = ratio
        print(f"Current firmware VDIV_RATIO set to {ratio:.2f}")
    
    def calibrate_with_multimeter(self, multimeter_voltage: float) -> float:
        """
        Calculate optimal VDIV_RATIO given multimeter measurement of actual voltage.
        
        Returns the calculated VDIV_RATIO that should be used in the firmware.
        """
        self.multimeter_reading = multimeter_voltage
        
        if not self.readings:
            raise ValueError("No readings available for calibration")
        
        # Use median of ADC readings for robustness
        adc_counts = [r.adc_raw for r in self.readings]
        adc_median = statistics.median(adc_counts)
        
        # Use median of calculated voltages
        calc_voltages = [r.voltage_v for r in self.readings]
        v_calc_median = statistics.median(calc_voltages)
        
        # VDIV_RATIO = V_actual / V_calculated
        self.calibrated_vdiv_ratio = multimeter_voltage / v_calc_median
        
        print(f"\n📊 Calibration Results:")
        print(f"  Multimeter reading: {multimeter_voltage:.3f} V")
        print(f"  Firmware V_calc (median): {v_calc_median:.3f} V")
        print(f"  ✓ Recommended VDIV_RATIO: {self.calibrated_vdiv_ratio:.2f}")
        print(f"\n  Update Config/ADC_Bringup.ino:")
        print(f"  #define VDIV_RATIO {self.calibrated_vdiv_ratio:.2f}f")
        
        return self.calibrated_vdiv_ratio
    
    def statistics(self) -> dict:
        """Compute descriptive statistics"""
        if not self.readings:
            return {}
        
        adc_counts = [r.adc_raw for r in self.readings]
        voltages = [r.voltage_v for r in self.readings]
        
        return {
            'count': len(self.readings),
            'adc_min': min(adc_counts),
            'adc_max': max(adc_counts),
            'adc_mean': statistics.mean(adc_counts),
            'adc_stdev': statistics.stdev(adc_counts) if len(adc_counts) > 1 else 0,
            'v_min': min(voltages),
            'v_max': max(voltages),
            'v_mean': statistics.mean(voltages),
            'v_stdev': statistics.stdev(voltages) if len(voltages) > 1 else 0,
        }
    
    def print_stats(self):
        """Print detailed statistics"""
        stats = self.statistics()
        
        if not stats:
            print("No statistics available")
            return
        
        print("\n📈 Statistics:")
        print(f"  Samples: {stats['count']}")
        print(f"\n  ADC Raw Counts:")
        print(f"    Min: {stats['adc_min']}")
        print(f"    Max: {stats['adc_max']}")
        print(f"    Mean: {stats['adc_mean']:.1f}")
        print(f"    StDev: {stats['adc_stdev']:.1f}")
        print(f"    Range: {stats['adc_max'] - stats['adc_min']}")
        
        print(f"\n  Calculated Voltage (with VDIV={self.current_vdiv_ratio:.2f}):")
        print(f"    Min: {stats['v_min']:.3f} V")
        print(f"    Max: {stats['v_max']:.3f} V")
        print(f"    Mean: {stats['v_mean']:.3f} V")
        print(f"    StDev: {stats['v_stdev']:.3f} V")
        print(f"    Range: {(stats['v_max'] - stats['v_min']):.3f} V")
        
        if self.multimeter_reading is not None:
            print(f"\n  Multimeter Reading: {self.multimeter_reading:.3f} V")
            if self.calibrated_vdiv_ratio is not None:
                print(f"  Calibrated VDIV_RATIO: {self.calibrated_vdiv_ratio:.2f}")
                # Recalculate what voltage would be with corrected ratio
                corrected_v_mean = stats['v_mean'] * self.calibrated_vdiv_ratio
                error = abs(corrected_v_mean - self.multimeter_reading)
                print(f"  V with calibrated ratio: {corrected_v_mean:.3f} V")
                print(f"  Error: {error:.4f} V ({error/self.multimeter_reading*100:.2f}%)")
    
    def recalculate_with_vdiv(self, vdiv_ratio: float) -> List[float]:
        """
        Recalculate voltages with a different VDIV_RATIO.
        
        Useful for testing what the output would be with a new calibration.
        """
        return [r.voltage_v * (vdiv_ratio / self.current_vdiv_ratio) 
                for r in self.readings]
    
    def plot(self, title: str = "ADC Bringup: Voltage Monitoring"):
        """Plot voltage trends over time"""
        if not HAS_MATPLOTLIB:
            print("❌ matplotlib not available. Install with: pip install matplotlib")
            return
        
        if not self.readings:
            print("No data to plot")
            return
        
        times = [r.uptime_s for r in self.readings]
        voltages_uncalibrated = [r.voltage_v for r in self.readings]
        
        fig, ax = plt.subplots(figsize=(12, 6))
        
        # Plot raw readings
        ax.plot(times, voltages_uncalibrated, 'o-', label='Firmware output', alpha=0.7)
        
        # Plot recalibrated if available
        if self.calibrated_vdiv_ratio is not None:
            voltages_calibrated = self.recalculate_with_vdiv(self.calibrated_vdiv_ratio)
            ax.plot(times, voltages_calibrated, 's--', label='Calibrated output', alpha=0.7)
            
            if self.multimeter_reading is not None:
                ax.axhline(self.multimeter_reading, color='red', linestyle=':', 
                          label=f'Multimeter: {self.multimeter_reading:.3f} V')
        
        ax.set_xlabel('Uptime (s)')
        ax.set_ylabel('Voltage (V)')
        ax.set_title(title)
        ax.grid(True, alpha=0.3)
        ax.legend()
        
        plt.tight_layout()
        plt.savefig('adc_calibration.png', dpi=150)
        print("✓ Plot saved to adc_calibration.png")
        plt.show()


def main():
    """Command-line interface"""
    import argparse
    
    parser = argparse.ArgumentParser(
        description='Analyze ADC bringup firmware output and calibrate voltage divider'
    )
    parser.add_argument('--serial-log', required=True, 
                       help='Serial output log file from ADC_Bringup.ino')
    parser.add_argument('--multimeter', type=float,
                       help='Actual voltage measured with multimeter (V)')
    parser.add_argument('--current-vdiv', type=float, default=1.0,
                       help='VDIV_RATIO used during firmware logging (default: 1.0)')
    parser.add_argument('--plot', action='store_true',
                       help='Generate calibration plot')
    
    args = parser.parse_args()
    
    try:
        adc = ADCAnalysis(args.serial_log)
        adc.set_current_vdiv_ratio(args.current_vdiv)
        adc.print_stats()
        
        if args.multimeter is not None:
            adc.calibrate_with_multimeter(args.multimeter)
        
        if args.plot:
            adc.plot()
        
    except (FileNotFoundError, ValueError) as e:
        print(f"❌ Error: {e}")
        sys.exit(1)


if __name__ == '__main__':
    main()
