#include <FS.h>

int lhe_getc(File *file, char *input) {
      if (file->readBytes(input,1)!=1) return -1;
      return 1;
}

unsigned char x2i(char input) {
    if (input >= '0' && input <= '9') {
        return input- '0';
    } else if (input >= 'A' && input <= 'F') {
        return input -('A' - 10);
    } else if (input >= 'a' && input <= 'f') {
        return input- ('a' - 10);
    }
    return -1;
}

int lhe_getb(File *file,char *input) {
   char temp[2];

      if (file->readBytes(&temp[0],2)!=2) return -1;
      *input = x2i(temp[1]) | (x2i(temp[0])<<4);
      return 1;
}

int lhe_getw(File *file, uint16_t *input) {
   char bytes[2];

   if (lhe_getb(file,&(bytes[0]))<0) return -1;
   if (lhe_getb(file,&(bytes[1]))<0) return -1;
   *input = bytes[0] << 8 | bytes[1];

   return 1;
}

int lhe_scan_eol(File *file, int max) {
int count = 0;
   while (max--) {
      char temp;
      if (file->readBytes(&temp,1)!=1) return -1;
      if (temp == 0x0A) break;
      count++;
   }
   return count;
}


int lhe_read_header(File *file, uint16_t *addr, uint8_t *csum) {
   char marker= 0x00;
   char count = 0x00;
   char rtype = 0x00;

      if (lhe_getc(file,&marker) != 1) {
         if (!file->available()) return -1;
         return  -2; // File Read Error
      }
      if (marker != ':') return -1;// Invalid file format

      if (lhe_getb(file,&count)  != 1) return  -1;
      if (lhe_getw(file,addr)   != 1) return  -1;
      if (lhe_getb(file,&rtype)  != 1) return  -1;

      if (rtype==0) {
         if (count>32) { // range error
            return -3;
         }

         return count;
      }
      return 0;
}

int lhe_read_payload(File *file,int count, char *data, uint8_t *csum) {
  // Copy COUNT bytes into data array
  while (count--) { // Populate the data array
     if (lhe_getb(file,data++) !=1) return -1;
  }
  if (lhe_getb(file,(char *)csum)  != 1) return  -1;
  return 0;
}

void lhe_dumpRec(uint16_t addr, uint8_t size, char * data) {
   int csum = 0x00;
   char tempstr[80];

   for (int i=0;i<size;i++) {
      csum -= data[i];
   }
   csum &= 0xFF;

   // Invoke the nRF Send function to send the 3 messages to the client
   sprintf(tempstr,"Size: %2d Addr: %2.2x csum:%2.2x\n", size, addr,csum);
   Serial.print(tempstr);
}


uint8_t lhe_read_record(File *file , uint16_t * addr, char * data ) {
   uint8_t  csum_pending=0x00;
   uint16_t count_pending = 0;
   uint8_t  done = 0;

   while (!done) {
      uint8_t  csum=0x00;
      uint16_t r_addr;
      int count = lhe_read_header(file, &r_addr, &csum);

      if (count>0) {
         if (count_pending) {
            // Trye and read second line of HEX
            lhe_read_payload(file,count,&(data[count_pending]),&csum);
            count_pending+=count;
            done = 1;
         } else {
            // First Read
            lhe_read_payload(file,count,data,&csum);
            count_pending= count;
            csum_pending = csum;
            *addr = r_addr>>1;
         }
      }  else {
         if (count_pending>0) {
            done = 1;
          }

      }
      // END of Line scan
      if (lhe_scan_eol(file,80)< 0){
         done = 0x01;
      }
   }

   return count_pending;
}

uint8_t lhe_read_record_at(File *file, uint32_t offset, uint16_t *addr,  char * data ){
   file->seek(offset,SeekSet);
   return lhe_read_record(file, addr, data);
}

void lhe_test(File *file) { // Pass in Open File Handle

   uint8_t done = 0x00;
   uint8_t lcount =0; // line Count

   char  data[32];
   uint8_t size;
   uint16_t addr;
   uint32_t last;


   while (file->available()) {
     last = file->position();
     size = lhe_read_record(file, &addr, data );
     if (size) {
        lcount++;
        lhe_dumpRec(addr, size, data) ;
     }
   }
   Serial.print(lcount);
   Serial.println(" lines of rtype=0");
}
