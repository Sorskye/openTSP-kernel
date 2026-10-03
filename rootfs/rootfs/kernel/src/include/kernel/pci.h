#include "types.h"
#ifndef PCI_H
#define PCIH

void pci_enumerate(void);

typedef struct {
    uint8_t class_code;
    uint8_t subclass;
    const char* description;
} pci_class_info_t;

#endif