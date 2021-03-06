#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "common.h"
#include "log.h"
#include "universe.h"
#include "civ.h"
#include "sector.h"
#include "planet.h"
#include "base.h"
#include "ptrlist.h"
#include "parseconfig.h"
#include "star.h"

void sector_init(struct sector *s)
{
	memset(s, 0, sizeof(*s));
	s->phi = 0.0;

	rb_init_node(&s->x_rbtree);
	ptrlist_init(&s->stars);
	ptrlist_init(&s->planets);
	ptrlist_init(&s->bases);
	ptrlist_init(&s->links);

	INIT_LIST_HEAD(&s->list);
}

void sector_free(struct sector *s) {
	struct list_head *lh;
	struct star *sol;
	struct planet *planet;
	struct base *base;

	if (s->name)
		free(s->name);

	if (s->gname)
		free(s->gname);

	ptrlist_for_each_entry(sol, &s->stars, lh)
		star_free(sol);
	ptrlist_free(&s->stars);

	ptrlist_for_each_entry(planet, &s->planets, lh)
		planet_free(planet);
	ptrlist_free(&s->planets);

	ptrlist_for_each_entry(base, &s->bases, lh)
		base_free(base);
	ptrlist_free(&s->bases);

	ptrlist_free(&s->links);
	free(s);
}

struct sector* sector_load(struct config *ctree)
{
	struct sector *s;
	struct list_head *lh;
	struct planet *p;
	struct base *b;
	struct star *sol;
	int haspos = 0;

	s = malloc(sizeof(*s));
	if (!s)
		return NULL;
	sector_init(s);

	while (ctree) {
		if (strcmp(ctree->key, "SECTOR") == 0) {

			s->name = NULL;

		} else if (strcmp(ctree->key, "PLANET") == 0) {

			p = malloc(sizeof(*p));
			if (!p)
				goto err;

			planet_load(p, ctree->sub);
			ptrlist_push(&s->planets, p);

		} else if (strcmp(ctree->key, "BASE") == 0) {

			b = malloc(sizeof(*b));
			if (!b)
				goto err;

			loadbase(b, ctree->sub);
			ptrlist_push(&s->bases, b);

		} else if (strcmp(ctree->key, "STAR") == 0) {

			sol = malloc(sizeof(*sol));
			if (!sol)
				goto err;

			star_load(sol, ctree->sub);
			ptrlist_push(&s->stars, sol);

		} else if (strcmp(ctree->key, "POS") == 0) {

			sscanf(ctree->data, "%lu %lu", &s->x, &s->y);
			haspos = 1;

		}
		ctree = ctree->next;
	}

	unsigned int totlum = 0;
	ptrlist_for_each_entry(sol, &s->stars, lh) {
		s->hab += sol->hab;
		totlum += sol->lumval;
	}
	if (!s->gname)
		die("%s", "required attribute missing in predefined sector: gname");
	if (!haspos)
		die("required attribute missing in predefined sector %s: position", s->name);

	s->hablow = star_gethablow(totlum);
	s->habhigh = star_gethabhigh(totlum);
	return s;

err:
	sector_free(s);
	return NULL;
}

#define STELLAR_MUL_HAB -50
int sector_create(struct sector *s, char *name)
{
	struct star *sol;
	struct list_head *lh;

	sector_init(s);

	s->name = strdup(name);

	if (star_populate_sector(s)) {
		free(s->name);
		return -1;
	}

	s->hab = 0;

	unsigned int totlum = 0;
	ptrlist_for_each_entry(sol, &s->stars, lh) {
		s->hab += sol->hab;
		totlum += sol->lumval;
	}
	s->hablow = star_gethablow(totlum);
	s->habhigh = star_gethabhigh(totlum);

	if (planet_populate_sector(s))
		return -1;

	printf("  Number of planets: %lu\n", ptrlist_len(&s->planets));

	return 0;
}

unsigned long sector_distance(const struct sector * const a, const struct sector * const b) {
	long result = sqrt( (double)(b->x - a->x)*(b->x - a->x) +
			(double)(b->y - a->y)*(b->y - a->y) );

	if (result < 0)
		return -result;

	return result;
}
