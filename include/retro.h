#ifndef _RETRO_H
#define _RETRO_H

#include "exti.h"

timeStamp * mainRetro;

#define FRONT 		interLine[11]
#define MIDDLE 		interLine[9]
#define REAR		interLine[13]

#define MAINFILTERINDEX 	(mainRetro->count % AVERAGE_SIZE)

#define MAIN_INTERVAL       (mainRetro->filter[MAINFILTERINDEX])

void initRetro(void);
int getVelocity(void);
#endif /* _RETR0_H*/
