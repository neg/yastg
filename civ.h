#ifndef _HAS_CIV_H
#define _HAS_CIV_H

#include "parseconfig.h"
#include "list.h"
#include "ptrlist.h"

struct universe;

struct civ {
	char* name;
	struct sector* home;
	int power;
	struct ptrlist presectors;
	struct ptrlist availnames;
	struct ptrlist sectors;
	struct ptrlist border_sectors;
	struct list_head list;
	struct list_head growing;
};

void loadciv(struct civ *c, struct config *ctree);
void civ_init(struct civ *c);
int civ_load_all(struct civ *civs);

void civ_spawncivs(struct universe *u, struct civ *civs);
void civ_free(struct civ *civ);

#endif
