#ifndef HEXPARSER_H_
#define HEXPARSER_H_

#include "FS.h"  // Defn of 'File'

int lhe_test(File *file);
uint8_t lhe_read_record(File *file, uint16_t *addr,  char * data );
uint8_t lhe_read_record_at(File *file, uint32_t offset, uint16_t *addr,  char * data );

#endif /* HEXPARSER_H_ */
