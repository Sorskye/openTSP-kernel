#include "types.h"
#ifndef PCI_H
#define PCI_H

void pci_enumerate(void);

typedef struct {
    uint8_t class_code;
    uint8_t subclass;
    const char* description;
} pci_class_info_t;

typedef enum{
    PCI_BAR_UNUSED,
    PCI_BAR_IO,
    PCI_BAR_MMIO32,
    PCI_BAR_MMIO64,
} pci_bar_type_t;

typedef struct{
    pci_bar_type_t type;

    uint64_t address;
    uint64_t size;

    bool prefetchable;
} pci_bar_t;

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;

    pci_bar_t bars[6];
    
} pci_device_t;

#endif