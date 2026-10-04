// Test-only correction for Renode 1.17 STM32F4 sector 4/5 geometry.
// The stock model erases 16 KiB for sector 4 and 64 KiB for sector 5;
// STM32F407VGT6 requires 64 KiB and 128 KiB respectively.
// All other register, unlock, program, status and option-byte behavior
// remains in Renode's STM32F4_FlashController implementation.
using Antmicro.Renode.Core;
using Antmicro.Renode.Peripherals.Memory;

namespace Antmicro.Renode.Peripherals.MTD
{
    public class HoodSTM32F407Flash : STM32F4_FlashController
    {
        public HoodSTM32F407Flash(IMachine machine, MappedMemory flash)
            : base(machine, flash)
        {
            this.flash = flash;
        }

        public override void WriteDoubleWord(long offset, uint value)
        {
            // CR STRT + SER, without MER. Read the lock state before the base
            // controller processes the write so a locked request cannot erase.
            var sector = (value >> 3) & 0xFu;
            var sectorErase = offset == 0x10 && (value & 0x10002u) == 0x10002u
                && (value & 0x4u) == 0 && (ReadDoubleWord(0x10) & 0x80000000u) == 0;
            base.WriteDoubleWord(offset, value);
            if(!sectorErase) return;
            if(sector == 4)
                flash.WriteBytes(0x14000, erasePattern, 0xC000);
            else if(sector == 5)
                flash.WriteBytes(0x30000, erasePattern, 0x10000);
        }

        private readonly MappedMemory flash;
        private static readonly byte[] erasePattern = new byte[0x10000];

        static HoodSTM32F407Flash()
        {
            for(var i = 0; i < erasePattern.Length; ++i) erasePattern[i] = 0xFF;
        }
    }
}
