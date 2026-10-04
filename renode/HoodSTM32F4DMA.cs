// Simulation-only DMA2 Stream 2 model for the F407 USART1 RX path.
// It models byte transfers, NDTR, circular reload, HT/TC flags and IRQs.
// Other streams and transfer modes are intentionally unsupported.
using System.Collections.Generic;
using System.Collections.ObjectModel;
using Antmicro.Renode.Core;
using Antmicro.Renode.Peripherals;
using Antmicro.Renode.Peripherals.Bus;

namespace Antmicro.Renode.Peripherals.DMA
{
    public class HoodSTM32F4DMA : IDoubleWordPeripheral, IKnownSize,
        IGPIOReceiver, INumberedGPIOOutput
    {
        public HoodSTM32F4DMA(IMachine machine)
        {
            engine = new DmaEngine(machine.GetSystemBus(this));
            var outputs = new Dictionary<int, IGPIO>();
            for(var i = 0; i < 8; i++) outputs[i] = new GPIO();
            Connections = new ReadOnlyDictionary<int, IGPIO>(outputs);
            Reset();
        }

        public IReadOnlyDictionary<int, IGPIO> Connections { get; }
        public long Size => 0x400;

        public void Reset()
        {
            config = ndtr = initial = peripheral = memory = fifo = flags = offset = 0;
            UpdateIRQ();
        }

        public uint ReadDoubleWord(long address)
        {
            switch(address)
            {
                case 0x00: return flags;
                case 0x04: return 0;
                case 0x08: case 0x0c: return 0;
                case 0x40: return config;
                case 0x44: return ndtr;
                case 0x48: return peripheral;
                case 0x4c: return memory;
                case 0x50: return 0;
                case 0x54: return fifo;
                default: return 0;
            }
        }

        public void WriteDoubleWord(long address, uint value)
        {
            switch(address)
            {
                case 0x08:
                    flags &= ~value;
                    UpdateIRQ();
                    break;
                case 0x40:
                    if((config & 1) == 0 && (value & 1) != 0) offset = 0;
                    config = value;
                    UpdateIRQ();
                    break;
                case 0x44:
                    ndtr = initial = value & 0xffff;
                    offset = 0;
                    break;
                case 0x48: peripheral = value; break;
                case 0x4c: memory = value; break;
                case 0x54: fifo = value; break;
            }
        }

        public void OnGPIO(int number, bool value)
        {
            if(number != 2 || !value || (config & 1) == 0 || ndtr == 0) return;
            // This test backend accepts only peripheral-to-memory byte transfers.
            if((config & (3u << 6)) != 0 || (config & (3u << 11)) != 0 ||
               (config & (3u << 13)) != 0) return;
            var target = memory + (((config & (1u << 10)) != 0) ? offset : 0);
            engine.IssueCopy(new Request(peripheral, target, 1,
                TransferType.Byte, TransferType.Byte, false, false));
            offset++;
            ndtr--;
            if(initial > 1 && ndtr == initial / 2) flags |= 1u << 20;
            if(ndtr == 0)
            {
                flags |= 1u << 21;
                if((config & (1u << 8)) != 0)
                {
                    ndtr = initial;
                    offset = 0;
                }
                else config &= ~1u;
            }
            UpdateIRQ();
        }

        private void UpdateIRQ()
        {
            Connections[2].Set(((flags & (1u << 20)) != 0 && (config & (1u << 3)) != 0) ||
                               ((flags & (1u << 21)) != 0 && (config & (1u << 4)) != 0));
        }

        private readonly DmaEngine engine;
        private uint config, ndtr, initial, peripheral, memory, fifo, flags, offset;
    }
}
