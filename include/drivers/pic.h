#ifndef PIC_H
#define PIC_H

void pic_remap();
void pic_send_eoi(int irq);
void pic_clear_mask(unsigned char irq_line);

#endif
