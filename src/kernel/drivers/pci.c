
#include "types.h"
#include "stdio.h"
#include "io.h"
#include"sleep.h"
#include "pci.h"
#include "serial.h"
#include "vmm.h"

static const pci_class_info_t pci_classes[] = {
    {0x00, 0x00, "Unclassified: Non-VGA device"},
    {0x00, 0x01, "Unclassified: VGA-compatible"},

    {0x01, 0x00, "Mass Storage: SCSI"},
    {0x01, 0x01, "Mass Storage: IDE"},
    {0x01, 0x02, "Mass Storage: Floppy"},
    {0x01, 0x03, "Mass Storage: IPI"},
    {0x01, 0x04, "Mass Storage: RAID"},
    {0x01, 0x05, "Mass Storage: ATA"},
    {0x01, 0x06, "Mass Storage: SATA"},
    {0x01, 0x07, "Mass Storage: SAS"},
    {0x01, 0x08, "Mass Storage: NVMe"},
    {0x01, 0x80, "Mass Storage: Other"},

    {0x02, 0x00, "Network: Ethernet"},
    {0x02, 0x01, "Network: Token Ring"},
    {0x02, 0x02, "Network: FDDI"},
    {0x02, 0x03, "Network: ATM"},
    {0x02, 0x80, "Network: Other"},

    {0x03, 0x00, "Display: VGA"},
    {0x03, 0x01, "Display: XGA"},
    {0x03, 0x02, "Display: 3D Controller"},
    {0x03, 0x80, "Display: Other"},

    {0x04, 0x00, "Multimedia: Video"},
    {0x04, 0x01, "Multimedia: Audio"},
    {0x04, 0x02, "Multimedia: Telephony"},
    {0x04, 0x03, "Multimedia: Audio Device"},
    {0x04, 0x80, "Multimedia: Other"},

    {0x05, 0x00, "Memory Controller: RAM"},
    {0x05, 0x01, "Memory Controller: Flash"},
    {0x05, 0x80, "Memory Controller: Other"},

    {0x06, 0x00, "Bridge: Host"},
    {0x06, 0x01, "Bridge: ISA"},
    {0x06, 0x02, "Bridge: EISA"},
    {0x06, 0x04, "Bridge: PCI-to-PCI"},
    {0x06, 0x07, "Bridge: CardBus"},
    {0x06, 0x09, "Bridge: PCI-to-PCI Subtractive"},
    {0x06, 0x80, "Bridge: Other"},

    {0x07, 0x00, "Simple Communications: Serial"},
    {0x07, 0x01, "Simple Communications: Parallel"},
    {0x07, 0x80, "Simple Communications: Other"},

    {0x08, 0x00, "Base System Peripheral: PIC"},
    {0x08, 0x01, "Base System Peripheral: DMA"},
    {0x08, 0x02, "Base System Peripheral: Timer"},
    {0x08, 0x03, "Base System Peripheral: RTC"},
    {0x08, 0x80, "Base System Peripheral: Other"},

    {0x09, 0x00, "Input Device: Keyboard"},
    {0x09, 0x01, "Input Device: Digitizer"},
    {0x09, 0x02, "Input Device: Mouse"},
    {0x09, 0x80, "Input Device: Other"},

    {0x0A, 0x00, "Docking Station"},
    {0x0A, 0x80, "Docking Station: Other"},

    {0x0B, 0x00, "Processor: 386"},
    {0x0B, 0x01, "Processor: 486"},
    {0x0B, 0x02, "Processor: Pentium"},
    {0x0B, 0x10, "Processor: Alpha"},
    {0x0B, 0x20, "Processor: PowerPC"},
    {0x0B, 0x30, "Processor: MIPS"},
    {0x0B, 0x40, "Processor: Co-Processor"},

    {0x0C, 0x00, "Serial Bus: FireWire"},
    {0x0C, 0x01, "Serial Bus: ACCESS Bus"},
    {0x0C, 0x02, "Serial Bus: SSA"},
    {0x0C, 0x03, "Serial Bus: USB"},
    {0x0C, 0x04, "Serial Bus: Fibre Channel"},
    {0x0C, 0x05, "Serial Bus: SMBus"},
    {0x0C, 0x80, "Serial Bus: Other"},
};

const char* pci_class_description(uint8_t class_code, uint8_t subclass)
{
   
    for (size_t i = 0; i < sizeof(pci_classes)/sizeof(pci_classes[0]); i++) {
        if (pci_classes[i].class_code == class_code &&
            pci_classes[i].subclass == subclass)
            return pci_classes[i].description;
    }
    return "Unknown PCI device";
}


// Returns the PCI configuration address for a device/function/register
uint32_t pci_config_address(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset)
{
    return (1U << 31)           // enable bit
         | ((uint32_t)bus << 16)
         | ((uint32_t)device << 11)
         | ((uint32_t)func << 8)
         | (offset & 0xFC);     // must be aligned to 4 bytes
}

// Read 32-bit PCI config value at given bus/device/function/offset
uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset)
{
    outl(0xCF8, pci_config_address(bus, device, func, offset));
    return inl(0xCFC);
}

void pci_config_write(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value){
    outl(0xCF8, pci_config_address(bus, device, func, offset));
    outl(0xCFC, value);
}


void pci_read_bar(uint8_t bus, uint8_t device, uint8_t func, uint8_t bar_index, pci_bar_t *bar){
    uint8_t offset = 0x10 + (bar_index * 4);
    uint32_t original = pci_config_read(bus, device, func, offset);

    if(original == 0){
        bar->type = PCI_BAR_UNUSED;
        return;
    }

    pci_config_write(bus, device, func, offset, 0xFFFFFFFF);
    uint32_t size_mask = pci_config_read(bus, device, func, offset);
    pci_config_write(bus, device, func, offset, original);

    if (original & 1){
        // I/O bar
        bar->type = PCI_BAR_IO;

        bar->address = original & ~0x3;
        bar->size = ~(size_mask & ~0x3) + 1;
        bar->prefetchable = false;

    }
    else{
        uint8_t mem_type = (original >> 1) & 0x3;

        if (mem_type == 2){
            // 64 bit bar
            uint32_t original_hi =
                pci_config_read(bus, device, func, offset + 4);

            pci_config_write(bus, device, func, offset + 4, 0xFFFFFFFF);
            uint32_t size_hi =
                pci_config_read(bus, device, func, offset + 4);
            pci_config_write(bus, device, func, offset + 4, original_hi);

            uint64_t addr =
                ((uint64_t)original_hi << 32) |
                (original & ~0xF);

            uint64_t mask =
                ((uint64_t)size_hi << 32) |
                (size_mask & ~0xFULL);

            bar->type = PCI_BAR_MMIO64;
            bar->address = addr;
            bar->size = ~mask + 1;
        }
        else{
            bar->type = PCI_BAR_MMIO32;
            bar->address = original & ~0xF;
            bar->size = ~(size_mask & ~0xF) + 1;
        }

        bar->prefetchable = (original & 8) != 0;
    }
}

// enumerate PCI devices and map structs to memory
void pci_enumerate(void)
{
    serial_print("enumerating pci devices\n");
    int countdev = 0;

    for (uint16_t bus = 0; bus < 256; bus++) {
        
        for (uint8_t device = 0; device < 32; device++) {

            uint32_t data = pci_config_read(bus, device, 0, 0x00);
            uint16_t vendor = data & 0xFFFF;

            if (vendor == 0xFFFF)
                continue; 

            
            uint32_t header = pci_config_read(bus, device, 0, 0x0C);
            uint8_t header_type = (header >> 16) & 0xFF;
            bool multifunction = header_type & 0x80;

            uint8_t maxfunc = multifunction ? 7 : 0;

            for (uint8_t func = 0; func <= maxfunc; func++) {

                uint32_t data2 = pci_config_read(bus, device, func, 0x00);
                uint32_t classreg = pci_config_read(bus, device, func, 0x08);

                uint8_t class_code = (classreg >> 24) & 0xFF;
                uint8_t subclass = (classreg >> 16) & 0xFF;
                uint8_t prog_if = (classreg >> 8) & 0xFF;
                uint16_t vendor2 = data2 & 0xFFFF;

                if (vendor2 == 0xFFFF)
                    continue; 

                uint16_t device_id = (data2 >> 16) & 0xFFFF;

                pci_device_t *dev = kzalloc(sizeof(pci_device_t));
                // create device
                
                dev->bus = bus;
                dev->device = device;
                dev->function = func;
                dev->vendor_id = vendor2;
                dev->device_id = device_id;
                dev->class_code = class_code;
                dev->subclass = subclass;
                dev->prog_if = prog_if;
                serial_print("PCI Device: %x:%x at %x:%x.%u\n", vendor2, device_id, bus, device, func);

                for (int i=0; i < 6; i++){
                    pci_read_bar(bus, device, func, i, &dev->bars[i]);

                    if (dev->bars[i].type != PCI_BAR_UNUSED){
                        serial_print("BAR%d: addr=%x size=%x type=%d\n",
                            i,
                            dev->bars[i].address,
                            dev->bars[i].size,
                            dev->bars[i].type
                        );

                        if (dev->bars[i].type == PCI_BAR_MMIO64){
                            i++;
                        }
                    }
                }

                // map device (struct) to memory
              
                countdev++;
            }
        }
    }

    serial_print("%d PCI devices found\n", countdev);
    return;
}
