#include "Bit_Struct.h"
#include "../WebPort/sf_web_math.h"


extern bit_list bits ;
#define ROT_LIMIT	((1024*1024)-1)

extern long which_graphics_set ;

void bit_update(void);

void add_bit( 	long , long , long ,
				long , long , long ,
				long , long ,
				long , long ,
				long , long );

