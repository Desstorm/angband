/**
 * \file ui-display.c
 * \brief Handles the setting up updating, and cleaning up of the game display.
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 * Copyright (c) 2007 Antony Sidwell
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#include "angband.h"
#include "buildid.h"
#include "cave.h"
#include "cmd-core.h"
#include "game-event.h"
#include "game-world.h"
#include "grafmode.h"
#include "hint.h"
#include "init.h"
#include "mon-lore.h"
#include "mon-util.h"
#include "monster.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-pile.h"
#include "obj-util.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "player-util.h"
#include "player.h"
#include "project.h"
#include "savefile.h"
#include "target.h"
#include "ui-birth.h"
#include "ui-display.h"
#include "ui-game.h"
#include "ui-input.h"
#include "ui-map.h"
#include "ui-mon-list.h"
#include "ui-mon-lore.h"
#include "ui-object.h"
#include "ui-obj-list.h"
#include "ui-output.h"
#include "ui-player.h"
#include "ui-prefs.h"
#include "ui-store.h"
#include "ui-term.h"
#include "wizard.h"

/**
 * There are a few functions installed to be triggered by several 
 * of the basic player events.  For convenience, these have been grouped 
 * in this list.
 */
static game_event_type player_events[] =
{
	EVENT_RACE_CLASS,
	EVENT_PLAYERTITLE,
	EVENT_EXPERIENCE,
	EVENT_PLAYERLEVEL,
	EVENT_GOLD,
	EVENT_EQUIPMENT,  /* For equippy chars */
	EVENT_STATS,
	EVENT_HP,
	EVENT_MANA,
	EVENT_AC,

	EVENT_MONSTERHEALTH,

	EVENT_PLAYERSPEED,
	EVENT_DUNGEONLEVEL,
};

static game_event_type statusline_events[] =
{
	EVENT_STUDYSTATUS,
	EVENT_STATUS,
	EVENT_STATE,
	EVENT_FEELING,
};

/**
 * Abbreviations of healthy stats
 */
const char *stat_names[STAT_MAX] =
{
	"STR: ", "INT: ", "WIS: ", "DEX: ", "CON: "
};

/**
 * Abbreviations of damaged stats
 */
const char *stat_names_reduced[STAT_MAX] =
{
	"Str: ", "Int: ", "Wis: ", "Dex: ", "Con: "
};

/**
 * Converts stat num into a six-char (right justified) string
 */
void cnv_stat(int val, char *out_val, size_t out_len)
{
	/* Stats above 18 need special treatment*/
	if (val > 18) {
		int bonus = (val - 18);

		if (bonus >= 220)
			strnfmt(out_val, out_len, "18/***");
		else if (bonus >= 100)
			strnfmt(out_val, out_len, "18/%03d", bonus);
		else
			strnfmt(out_val, out_len, " 18/%02d", bonus);
	} else {
		strnfmt(out_val, out_len, "    %2d", val);
	}
}

/**
 * ------------------------------------------------------------------------
 * Sidebar display functions
 * ------------------------------------------------------------------------ */

/**
 * Print character info at given row, column in a 13 char field
 */
static void prt_field(const char *info, int row, int col)
{
	/* Dump 13 spaces to clear */
	c_put_str(COLOUR_WHITE, "             ", row, col);

	/* Dump the info itself */
	c_put_str(COLOUR_L_BLUE, info, row, col);
}


/**
 * Print character stat in given row, column
 */
static void prt_stat(int stat, int row, int col)
{
	char tmp[32];

	/* Injured or healthy stat */
	if (player->stat_cur[stat] < player->stat_max[stat]) {
		put_str(stat_names_reduced[stat], row, col);
		cnv_stat(player->state.stat_use[stat], tmp, sizeof(tmp));
		c_put_str(COLOUR_YELLOW, tmp, row, col + 6);
	} else {
		put_str(stat_names[stat], row, col);
		cnv_stat(player->state.stat_use[stat], tmp, sizeof(tmp));
		c_put_str(COLOUR_L_GREEN, tmp, row, col + 6);
	}

	/* Indicate natural maximum */
	if (player->stat_max[stat] == 18+100)
		put_str("!", row, col + 3);
}


/**
 * Prints "title", including "wizard" or "winner" as needed.
 */
static void prt_title(int row, int col)
{
	const char *p;

	/* Wizard, winner or neither */
	if (player->wizard)
		p = "[=-WIZARD-=]";
	else if (player->total_winner || (player->lev > PY_MAX_LEVEL))
		p = "***WINNER***";
	else
		p = player->class->title[(player->lev - 1) / 5];

	prt_field(p, row, col);
}


/**
 * Prints level
 */
static void prt_level(int row, int col)
{
	char tmp[32];

	strnfmt(tmp, sizeof(tmp), "%6d", player->lev);

	if (player->lev >= player->max_lev) {
		put_str("LEVEL ", row, col);
		c_put_str(COLOUR_L_GREEN, tmp, row, col + 6);
	} else {
		put_str("Level ", row, col);
		c_put_str(COLOUR_YELLOW, tmp, row, col + 6);
	}
}


/**
 * Display the experience
 */
static void prt_exp(int row, int col)
{
	char out_val[32];
	bool lev50 = (player->lev == 50);

	long xp = (long)player->exp;


	/* Calculate XP for next level */
	if (!lev50)
		xp = (long)(player_exp[player->lev - 1] * player->expfact / 100L) -
			player->exp;

	/* Format XP */
	strnfmt(out_val, sizeof(out_val), "%8d", xp);


	if (player->exp >= player->max_exp) {
		put_str((lev50 ? "EXP" : "NXT"), row, col);
		c_put_str(COLOUR_L_GREEN, out_val, row, col + 4);
	} else {
		put_str((lev50 ? "Exp" : "Nxt"), row, col);
		c_put_str(COLOUR_YELLOW, out_val, row, col + 4);
	}
}


/**
 * Prints current gold
 */
static void prt_gold(int row, int col)
{
	char tmp[32];

	put_str("AU ", row, col);
	strnfmt(tmp, sizeof(tmp), "%9d", player->au);
	c_put_str(COLOUR_L_GREEN, tmp, row, col + 3);
}


/**
 * Equippy chars (ASCII representation of gear in equipment slot order)
 */
static void prt_equippy(int row, int col)
{
	int i;

	byte a;
	wchar_t c;

	struct object *obj;

	/* No equippy chars in bigtile mode */
	if (tile_width > 1 || tile_height > 1) return;

	/* Dump equippy chars */
	for (i = 0; i < player->body.count; i++) {
		/* Object */
		obj = slot_object(player, i);

		if (obj) {
			c = object_char(obj);
			a = object_attr(obj);
		} else {
			c = ' ';
			a = COLOUR_WHITE;
		}

		/* Dump */
		Term_putch(col + i, row, a, c);
	}
}


/**
 * Prints current AC
 */
static void prt_ac(int row, int col)
{
	char tmp[32];

	put_str("Cur AC ", row, col);
	strnfmt(tmp, sizeof(tmp), "%5d", 
			player->known_state.ac + player->known_state.to_a);
	c_put_str(COLOUR_L_GREEN, tmp, row, col + 7);
}

/**
 * Prints current hitpoints
 */
static void prt_hp(int row, int col)
{
	char cur_hp[32], max_hp[32];
	byte color = player_hp_attr(player);

	put_str("HP ", row, col);

	strnfmt(max_hp, sizeof(max_hp), "%4d", player->mhp);
	strnfmt(cur_hp, sizeof(cur_hp), "%4d", player->chp);
	
	c_put_str(color, cur_hp, row, col + 3);
	c_put_str(COLOUR_WHITE, "/", row, col + 7);
	c_put_str(COLOUR_L_GREEN, max_hp, row, col + 8);
}

/**
 * Prints players max/cur spell points
 */
static void prt_sp(int row, int col)
{
	char cur_sp[32], max_sp[32];
	byte color = player_sp_attr(player);

	/* Do not show mana unless we should have some */
	if (player_has(player, PF_NO_MANA) || 
		(player->lev < player->class->magic.spell_first))
		return;

	put_str("SP ", row, col);

	strnfmt(max_sp, sizeof(max_sp), "%4d", player->msp);
	strnfmt(cur_sp, sizeof(cur_sp), "%4d", player->csp);

	/* Show mana */
	c_put_str(color, cur_sp, row, col + 3);
	c_put_str(COLOUR_WHITE, "/", row, col + 7);
	c_put_str(COLOUR_L_GREEN, max_sp, row, col + 8);
}

/**
 * Calculate the monster bar color separately, for ports.
 */
byte monster_health_attr(void)
{
	struct monster *mon = player->upkeep->health_who;
	byte attr;

	if (!mon) {
		/* Not tracking */
		attr = COLOUR_DARK;

	} else if (!mflag_has(mon->mflag, MFLAG_VISIBLE) || mon->hp < 0 ||
			   player->timed[TMD_IMAGE]) {
		/* The monster health is "unknown" */
		attr = COLOUR_WHITE;

	} else {
		int pct;

		/* Default to almost dead */
		attr = COLOUR_RED;

		/* Extract the "percent" of health */
		pct = 100L * mon->hp / mon->maxhp;

		/* Badly wounded */
		if (pct >= 10) attr = COLOUR_L_RED;

		/* Wounded */
		if (pct >= 25) attr = COLOUR_ORANGE;

		/* Somewhat Wounded */
		if (pct >= 60) attr = COLOUR_YELLOW;

		/* Healthy */
		if (pct >= 100) attr = COLOUR_L_GREEN;

		/* Afraid */
		if (mon->m_timed[MON_TMD_FEAR]) attr = COLOUR_VIOLET;

		/* Confused */
		if (mon->m_timed[MON_TMD_CONF]) attr = COLOUR_UMBER;

		/* Stunned */
		if (mon->m_timed[MON_TMD_STUN]) attr = COLOUR_L_BLUE;

		/* Asleep */
		if (mon->m_timed[MON_TMD_SLEEP]) attr = COLOUR_BLUE;
	}
	
	return attr;
}

/**
 * Redraw the "monster health bar"
 *
 * The "monster health bar" provides visual feedback on the "health"
 * of the monster currently being "tracked".  There are several ways
 * to "track" a monster, including targetting it, attacking it, and
 * affecting it (and nobody else) with a ranged attack.  When nothing
 * is being tracked, we clear the health bar.  If the monster being
 * tracked is not currently visible, a special health bar is shown.
 */
static void prt_health(int row, int col)
{
	byte attr = monster_health_attr();
	struct monster *mon = player->upkeep->health_who;

	/* Not tracking */
	if (!mon) {
		/* Erase the health bar */
		Term_erase(col, row, 12);
		return;
	}

	/* Tracking an unseen, hallucinatory, or dead monster */
	if (!mflag_has(mon->mflag, MFLAG_VISIBLE) || /* Unseen */
		(player->timed[TMD_IMAGE]) || /* Hallucination */
		(mon->hp < 0)) { /* Dead (?) */
		/* The monster health is "unknown" */
		Term_putstr(col, row, 12, attr, "[----------]");
	} else { /* Visible */
		/* Extract the "percent" of health */
		int pct = 100L * mon->hp / mon->maxhp;

		/* Convert percent into "health" */
		int len = (pct < 10) ? 1 : (pct < 90) ? (pct / 10 + 1) : 10;

		/* Default to "unknown" */
		Term_putstr(col, row, 12, COLOUR_WHITE, "[----------]");

		/* Dump the current "health" (use '*' symbols) */
		Term_putstr(col + 1, row, len, attr, "**********");
	}
}


/**
 * Prints the speed of a character.
 */
static void prt_speed(int row, int col)
{
	int i = player->state.speed;

	byte attr = COLOUR_WHITE;
	const char *type = NULL;
	char buf[32] = "";

	/* 110 is normal speed, and requires no display */
	if (i > 110) {
		attr = COLOUR_L_GREEN;
		type = "Fast";
	} else if (i < 110) {
		attr = COLOUR_L_UMBER;
		type = "Slow";
	}

	if (type)
		strnfmt(buf, sizeof(buf), "%s (%+d)", type, (i - 110));

	/* Display the speed */
	c_put_str(attr, format("%-10s", buf), row, col);
}


/**
 * Prints depth in stat area
 */
static void prt_depth(int row, int col)
{
	char depths[32];

	if (!player->depth)
		my_strcpy(depths, "Town", sizeof(depths));
	else
		strnfmt(depths, sizeof(depths), "%d' (L%d)",
		        player->depth * 50, player->depth);

	/* Right-Adjust the "depth", and clear old values */
	put_str(format("%-13s", depths), row, col);
}




/**
 * Some simple wrapper functions
 */
static void prt_str(int row, int col) { prt_stat(STAT_STR, row, col); }
static void prt_dex(int row, int col) { prt_stat(STAT_DEX, row, col); }
static void prt_wis(int row, int col) { prt_stat(STAT_WIS, row, col); }
static void prt_int(int row, int col) { prt_stat(STAT_INT, row, col); }
static void prt_con(int row, int col) { prt_stat(STAT_CON, row, col); }
static void prt_race(int row, int col) { prt_field(player->race->name, row, col); }
static void prt_class(int row, int col) { prt_field(player->class->name, row, col); }


/**
 * Struct of sidebar handlers.
 */
static const struct side_handler_t
{
	void (*hook)(int, int);	 /* int row, int col */
	int priority;		 /* 1 is most important (always displayed) */
	game_event_type type;	 /* PR_* flag this corresponds to */
} side_handlers[] = {
	{ prt_race,    19, EVENT_RACE_CLASS },
	{ prt_title,   18, EVENT_PLAYERTITLE },
	{ prt_class,   22, EVENT_RACE_CLASS },
	{ prt_level,   10, EVENT_PLAYERLEVEL },
	{ prt_exp,     16, EVENT_EXPERIENCE },
	{ prt_gold,    11, EVENT_GOLD },
	{ prt_equippy, 17, EVENT_EQUIPMENT },
	{ prt_str,      6, EVENT_STATS },
	{ prt_int,      5, EVENT_STATS },
	{ prt_wis,      4, EVENT_STATS },
	{ prt_dex,      3, EVENT_STATS },
	{ prt_con,      2, EVENT_STATS },
	{ NULL,        15, 0 },
	{ prt_ac,       7, EVENT_AC },
	{ prt_hp,       8, EVENT_HP },
	{ prt_sp,       9, EVENT_MANA },
	{ NULL,        21, 0 },
	{ prt_health,  12, EVENT_MONSTERHEALTH },
	{ NULL,        20, 0 },
	{ NULL,        22, 0 },
	{ prt_speed,   13, EVENT_PLAYERSPEED }, /* Slow (-NN) / Fast (+NN) */
	{ prt_depth,   14, EVENT_DUNGEONLEVEL }, /* Lev NNN / NNNN ft */
};


/**
 * This prints the sidebar, using a clever method which means that it will only
 * print as much as can be displayed on <24-line screens.
 *
 * Each row is given a priority; the least important higher numbers and the most
 * important lower numbers.  As the screen gets smaller, the rows start to
 * disappear in the order of lowest to highest importance.
 */
static void update_sidebar(game_event_type type, game_event_data *data,
						   void *user)
{
	int x, y, row;
	int max_priority;
	size_t i;


	Term_get_size(&x, &y);

	/* Keep the top and bottom lines clear. */
	max_priority = y - 2;

	/* Display list entries */
	for (i = 0, row = 1; i < N_ELEMENTS(side_handlers); i++) {
		const struct side_handler_t *hnd = &side_handlers[i];
		int priority = hnd->priority;
		bool from_bottom = false;

		/* Negative means print from bottom */
		if (priority < 0) {
			priority = -priority;
			from_bottom = true;
		}

		/* If this is high enough priority, display it */
		if (priority <= max_priority) {
			if (hnd->type == type && hnd->hook) {
				if (from_bottom)
					hnd->hook(Term->hgt - (N_ELEMENTS(side_handlers) - i), 0);
				else
				    hnd->hook(row, 0);
			}

			/* Increment for next time */
			row++;
		}
	}
}

/**
 * Redraw player, since the player's color indicates approximate health.  Note
 * that using this command is only for when graphics mode is off, as
 * otherwise it causes the character to be a black square.
 */
static void hp_colour_change(game_event_type type, game_event_data *data,
							 void *user)
{
	if ((OPT(player, hp_changes_color)) && (use_graphics == GRAPHICS_NONE))
		square_light_spot(cave, player->py, player->px);
}



/**
 * ------------------------------------------------------------------------
 * Status line display functions
 * ------------------------------------------------------------------------ */

/**
 * Simple macro to initialise structs
 */
#define S(s)		s, sizeof(s)

/**
 * Struct to describe different timed effects
 */
struct state_info
{
	int value;
	const char *str;
	size_t len;
	byte attr;
};

/**
 * TMD_CUT descriptions
 */
static const struct state_info cut_data[] =
{
	{ 1000, S("Mortal wound"), COLOUR_L_RED },
	{  200, S("Deep gash"),    COLOUR_RED },
	{  100, S("Severe cut"),   COLOUR_RED },
	{   50, S("Nasty cut"),    COLOUR_ORANGE },
	{   25, S("Bad cut"),      COLOUR_ORANGE },
	{   10, S("Light cut"),    COLOUR_YELLOW },
	{    0, S("Graze"),        COLOUR_YELLOW },
};

/**
 * TMD_STUN descriptions
 */
static const struct state_info stun_data[] =
{
	{   100, S("Knocked out"), COLOUR_RED },
	{    50, S("Heavy stun"),  COLOUR_ORANGE },
	{     0, S("Stun"),        COLOUR_ORANGE },
};

/**
 * player->hunger descriptions
 */
static const struct state_info hunger_data[] =
{
	{ PY_FOOD_FAINT, S("Faint"),    COLOUR_RED },
	{ PY_FOOD_WEAK,  S("Weak"),     COLOUR_ORANGE },
	{ PY_FOOD_ALERT, S("Hungry"),   COLOUR_YELLOW },
	{ PY_FOOD_FULL,  S(""),         COLOUR_L_GREEN },
	{ PY_FOOD_MAX,   S("Full"),     COLOUR_L_GREEN },
};

/**
 * For the various TMD_* effects
 */
static const struct state_info effects[] =
{
	{ TMD_BLIND,     S("Blind"),      COLOUR_ORANGE },
	{ TMD_PARALYZED, S("Paralyzed!"), COLOUR_RED },
	{ TMD_CONFUSED,  S("Confused"),   COLOUR_ORANGE },
	{ TMD_AFRAID,    S("Afraid"),     COLOUR_ORANGE },
	{ TMD_TERROR,    S("Terror"),     COLOUR_RED },
	{ TMD_IMAGE,     S("Halluc"),     COLOUR_ORANGE },
	{ TMD_POISONED,  S("Poisoned"),   COLOUR_ORANGE },
	{ TMD_PROTEVIL,  S("ProtEvil"),   COLOUR_L_GREEN },
	{ TMD_SPRINT,    S("Sprint"),     COLOUR_L_GREEN },
	{ TMD_TRAPSAFE,  S("TrapSafe"),   COLOUR_L_GREEN },
	{ TMD_TELEPATHY, S("ESP"),        COLOUR_L_BLUE },
	{ TMD_INVULN,    S("Invuln"),     COLOUR_L_GREEN },
	{ TMD_HERO,      S("Hero"),       COLOUR_L_GREEN },
	{ TMD_SHERO,     S("Berserk"),    COLOUR_L_GREEN },
	{ TMD_BOLD,      S("Bold"),       COLOUR_L_GREEN },
	{ TMD_STONESKIN, S("Stone"),      COLOUR_L_GREEN },
	{ TMD_SHIELD,    S("Shield"),     COLOUR_L_GREEN },
	{ TMD_BLESSED,   S("Blssd"),      COLOUR_L_GREEN },
	{ TMD_SINVIS,    S("SInvis"),     COLOUR_L_GREEN },
	{ TMD_SINFRA,    S("Infra"),      COLOUR_L_GREEN },
	{ TMD_OPP_ACID,  S("RAcid"),      COLOUR_SLATE },
	{ TMD_OPP_ELEC,  S("RElec"),      COLOUR_BLUE },
	{ TMD_OPP_FIRE,  S("RFire"),      COLOUR_RED },
	{ TMD_OPP_COLD,  S("RCold"),      COLOUR_WHITE },
	{ TMD_OPP_POIS,  S("RPois"),      COLOUR_GREEN },
	{ TMD_OPP_CONF,  S("RConf"),      COLOUR_VIOLET },
	{ TMD_AMNESIA,   S("Amnesiac"),   COLOUR_ORANGE },
	{ TMD_SCRAMBLE,   S("Scrambled"),   COLOUR_VIOLET },
};

#define PRINT_STATE(sym, data, index, row, col) \
{ \
	size_t i; \
	\
	for (i = 0; i < N_ELEMENTS(data); i++) \
	{ \
		if (index sym data[i].value) \
		{ \
			if (data[i].str[0]) \
			{ \
				c_put_str(data[i].attr, data[i].str, row, col); \
				return data[i].len; \
			} \
			else \
			{ \
				return 0; \
			} \
		} \
	} \
}


/**
 * Print recall status.
 */
static size_t prt_recall(int row, int col)
{
	if (player->word_recall) {
		c_put_str(COLOUR_WHITE, "Recall", row, col);
		return sizeof "Recall";
	}

	return 0;
}


/**
 * Print deep descent status.
 */
static size_t prt_descent(int row, int col)
{
	if (player->deep_descent) {
		c_put_str(COLOUR_WHITE, "Descent", row, col);
		return sizeof "Descent";
	}

	return 0;
}


/**
 * Print cut indicator.
 */
static size_t prt_cut(int row, int col)
{
	PRINT_STATE(>, cut_data, player->timed[TMD_CUT], row, col);
	return 0;
}


/**
 * Print stun indicator.
 */
static size_t prt_stun(int row, int col)
{
	PRINT_STATE(>, stun_data, player->timed[TMD_STUN], row, col);
	return 0;
}


/**
 * Prints status of hunger
 */
static size_t prt_hunger(int row, int col)
{
	PRINT_STATE(<=, hunger_data, player->food, row, col);
	return 0;
}



/**
 * Prints Resting, or 'count' status
 * Display is always exactly 10 characters wide (see below)
 *
 * This function was a major bottleneck when resting, so a lot of
 * the text formatting code was optimized in place below.
 */
static size_t prt_state(int row, int col)
{
	byte attr = COLOUR_WHITE;

	char text[16] = "";


	/* Displayed states are resting and repeating */
	if (player_is_resting(player)) {
		int i;
		int n = player_resting_count(player);

		/* Start with "Rest" */
		my_strcpy(text, "Rest      ", sizeof(text));

		/* Display according to length or intent of rest */
		if (n >= 1000) {
			i = n / 100;
			text[9] = '0';
			text[8] = '0';
			text[7] = I2D(i % 10);
			if (i >= 10) {
				i = i / 10;
				text[6] = I2D(i % 10);
				if (i >= 10)
					text[5] = I2D(i / 10);
			}
		} else if (n >= 100) {
			i = n;
			text[9] = I2D(i % 10);
			i = i / 10;
			text[8] = I2D(i % 10);
			text[7] = I2D(i / 10);
		} else if (n >= 10) {
			i = n;
			text[9] = I2D(i % 10);
			text[8] = I2D(i / 10);
		} else if (n > 0) {
			i = n;
			text[9] = I2D(i);
		} else if (n == REST_ALL_POINTS)
			text[5] = text[6] = text[7] = text[8] = text[9] = '*';
		else if (n == REST_COMPLETE)
			text[5] = text[6] = text[7] = text[8] = text[9] = '&';
		else if (n == REST_SOME_POINTS)
			text[5] = text[6] = text[7] = text[8] = text[9] = '!';

	} else if (cmd_get_nrepeats()) {
		int nrepeats = cmd_get_nrepeats();

		if (nrepeats > 999)
			strnfmt(text, sizeof(text), "Rep. %3d00", nrepeats / 100);
		else
			strnfmt(text, sizeof(text), "Repeat %3d", nrepeats);
	}

	/* Display the info (or blanks) */
	c_put_str(attr, text, row, col);

	return strlen(text);
}

static const byte obj_feeling_color[] = 
{
	/* Colors used to display each obj feeling 	*/
	COLOUR_WHITE,  /* "Looks like any other level." */
	COLOUR_L_PURPLE, /* "you sense an item of wondrous power!" */
	COLOUR_L_RED, /* "there are superb treasures here." */
	COLOUR_ORANGE, /* "there are excellent treasures here." */
	COLOUR_YELLOW, /* "there are very good treasures here." */
	COLOUR_YELLOW, /* "there are good treasures here." */
	COLOUR_L_GREEN, /* "there may be something worthwhile here." */
	COLOUR_L_GREEN, /* "there may not be much interesting here." */
	COLOUR_L_GREEN, /* "there aren't many treasures here." */
	COLOUR_L_BLUE, /* "there are only scraps of junk here." */
	COLOUR_L_BLUE  /* "there are naught but cobwebs here. */
};

static const byte mon_feeling_color[] = 
{
	/* Colors used to display each monster feeling */
	COLOUR_WHITE, /* "You are still uncertain about this place" */
	COLOUR_RED, /* "Omens of death haunt this place" */
	COLOUR_ORANGE, /* "This place seems murderous" */
	COLOUR_ORANGE, /* "This place seems terribly dangerous" */
	COLOUR_YELLOW, /* "You feel anxious about this place" */
	COLOUR_YELLOW, /* "You feel nervous about this place" */
	COLOUR_GREEN, /* "This place does not seem too risky" */
	COLOUR_GREEN, /* "This place seems reasonably safe" */
	COLOUR_BLUE, /* "This seems a tame, sheltered place" */
	COLOUR_BLUE, /* "This seems a quiet, peaceful place" */
};

/**
 * Prints level feelings at status if they are enabled.
 */
static size_t prt_level_feeling(int row, int col)
{
	u16b obj_feeling;
	u16b mon_feeling;
	char obj_feeling_str[6];
	char mon_feeling_str[6];
	int new_col;
	byte obj_feeling_color_print;

	/* Don't show feelings for cold-hearted characters */
	if (!OPT(player, birth_feelings)) return 0;

	/* No useful feeling in town */
	if (!player->depth) return 0;

	/* Get feelings */
	obj_feeling = cave->feeling / 10;
	mon_feeling = cave->feeling - (10 * obj_feeling);

	/*
	 *   Convert object feeling to a symbol easier to parse
	 * for a human.
	 *   0 -> * "Looks like any other level."
	 *   1 -> $ "you sense an item of wondrous power!" (special feeling)
	 *   2 to 10 are feelings from 2 meaning superb feeling to 10
	 * meaning naught but cowebs.
	 *   It is easier for the player to have poor feelings as a
	 * low number and superb feelings as a higher one. So for
	 * display we reverse this numbers and substract 1.
	 *   Thus (2-10) becomes (1-9 reversed)
	 *
	 *   But before that check if the player has explored enough
	 * to get a feeling. If not display as ?
	 */
	if (cave->feeling_squares < z_info->feeling_need) {
		my_strcpy(obj_feeling_str, "?", sizeof(obj_feeling_str));
		obj_feeling_color_print = COLOUR_WHITE;
	} else {
		obj_feeling_color_print = obj_feeling_color[obj_feeling];
		if (obj_feeling == 0)
			my_strcpy(obj_feeling_str, "*", sizeof(obj_feeling_str));
		else if (obj_feeling == 1)
			my_strcpy(obj_feeling_str, "$", sizeof(obj_feeling_str));
		else
			strnfmt(obj_feeling_str, 5, "%d", (unsigned int) (11-obj_feeling));
	}

	/* 
	 *   Convert monster feeling to a symbol easier to parse
	 * for a human.
	 *   0 -> ? . Monster feeling should never be 0, but we check
	 * it just in case.
	 *   1 to 9 are feelings from omens of death to quiet, paceful.
	 * We also reverse this so that what we show is a danger feeling.
	 */
	if (mon_feeling == 0)
		my_strcpy( mon_feeling_str, "?", sizeof(mon_feeling_str) );
	else
		strnfmt(mon_feeling_str, 5, "%d", (unsigned int) ( 10-mon_feeling ));

	/* Display it */
	c_put_str(COLOUR_WHITE, "LF:", row, col);
	new_col = col + 3;
	c_put_str(mon_feeling_color[mon_feeling], mon_feeling_str, row, new_col);
	new_col += strlen( mon_feeling_str );
	c_put_str(COLOUR_WHITE, "-", row, new_col);
	++new_col;
	c_put_str(obj_feeling_color_print, obj_feeling_str,	row, new_col);
	new_col += strlen( obj_feeling_str ) + 1;

	return new_col - col;
}

/**
 * Print how many spells the player can study.
 */
static size_t prt_study(int row, int col)
{
	char *text;
	int attr = COLOUR_WHITE;

	/* Can the player learn new spells? */
	if (player->upkeep->new_spells) {
		/* If the player does not carry a book with spells they can study,
		   the message is displayed in a darker colour */
		if (!player_book_has_unlearned_spells(player))
			attr = COLOUR_L_DARK;

		/* Print study message */
		text = format("Study (%d)", player->upkeep->new_spells);
		c_put_str(attr, text, row, col);
		return strlen(text) + 1;
	}

	return 0;
}


/**
 * Print all timed effects.
 */
static size_t prt_tmd(int row, int col)
{
	size_t i, len = 0;

	for (i = 0; i < N_ELEMENTS(effects); i++)
		if (player->timed[effects[i].value]) {
			c_put_str(effects[i].attr, effects[i].str, row, col + len);
			len += effects[i].len;
		}

	return len;
}

/**
 * Print "unignoring" status
 */
static size_t prt_unignore(int row, int col)
{
	if (player->unignoring) {
		const char *str = "Unignoring";
		put_str(str, row, col);
		return strlen(str) + 1;
	}

	return 0;
}

/**
 * Descriptive typedef for status handlers
 */
typedef size_t status_f(int row, int col);

static status_f *status_handlers[] =
{ prt_level_feeling, prt_unignore, prt_recall, prt_descent, prt_state, prt_cut, 
  prt_stun, prt_hunger, prt_study, prt_tmd };


/**
 * Print the status line.
 */
static void update_statusline(game_event_type type, game_event_data *data, void *user)
{
	int row = Term->hgt - 1;
	int col = 13;
	size_t i;

	/* Clear the remainder of the line */
	prt("", row, col);

	/* Display those which need redrawing */
	for (i = 0; i < N_ELEMENTS(status_handlers); i++)
		col += status_handlers[i](row, col);
}


/**
 * ------------------------------------------------------------------------
 * Map redraw.
 * ------------------------------------------------------------------------ */

#ifdef MAP_DEBUG
static void trace_map_updates(game_event_type type, game_event_data *data,
							  void *user)
{
	if (data->point.x == -1 && data->point.y == -1)
		printf("Redraw whole map\n");
	else
		printf("Redraw (%i, %i)\n", data->point.x, data->point.y);
}
#endif

/**
 * Update either a single map grid or a whole map
 */
static void update_maps(game_event_type type, game_event_data *data, void *user)
{
	term *t = user;

	/* This signals a whole-map redraw. */
	if (data->point.x == -1 && data->point.y == -1)
		prt_map();

	/* Single point to be redrawn */
	else {
		struct grid_data g;
		int a, ta;
		wchar_t c, tc;

		int ky, kx;
		int vy, vx;

		/* Location relative to panel */
		ky = data->point.y - t->offset_y;
		kx = data->point.x - t->offset_x;

		if (t == angband_term[0]) {
			/* Verify location */
			if ((ky < 0) || (ky >= SCREEN_HGT)) return;

			/* Verify location */
			if ((kx < 0) || (kx >= SCREEN_WID)) return;

			/* Location in window */
			vy = ky + ROW_MAP;
			vx = kx + COL_MAP;

			if (tile_width > 1)
				vx += (tile_width - 1) * kx;

			if (tile_height > 1)
				vy += (tile_height - 1) * ky;

		} else {
			if (tile_width > 1)
			        kx += (tile_width - 1) * kx;

			if (tile_height > 1)
			        ky += (tile_height - 1) * ky;

			
			/* Verify location */
			if ((ky < 0) || (ky >= t->hgt)) return;
			if ((kx < 0) || (kx >= t->wid)) return;

			/* Location in window */
			vy = ky;
			vx = kx;
		}


		/* Redraw the grid spot */
		map_info(data->point.y, data->point.x, &g);
		grid_data_as_text(&g, &a, &c, &ta, &tc);
		Term_queue_char(t, vx, vy, a, c, ta, tc);
#ifdef MAP_DEBUG
		/* Plot 'spot' updates in light green to make them visible */
		Term_queue_char(t, vx, vy, COLOUR_L_GREEN, c, ta, tc);
#endif

		if ((tile_width > 1) || (tile_height > 1))
			Term_big_queue_char(t, vx, vy, a, c, COLOUR_WHITE, ' ');
	}

	/* Refresh the main screen unless the map needs to center */
	if (player->upkeep->update & (PU_PANEL) && OPT(player, center_player)) {
		int hgt = (t == angband_term[0]) ? SCREEN_HGT / 2 : t->hgt / 2;
		int wid = (t == angband_term[0]) ? SCREEN_WID / 2 : t->wid / 2;

		if (panel_should_modify(t, player->py - hgt, player->px - wid))
			return;
	}

	Term_fresh();
}

/**
 * ------------------------------------------------------------------------
 * Animations.
 * ------------------------------------------------------------------------ */

static bool animations_allowed = true;
static byte flicker = 0;
static byte color_flicker[MAX_COLORS][3] = 
{
	{COLOUR_DARK, COLOUR_L_DARK, COLOUR_L_RED},
	{COLOUR_WHITE, COLOUR_L_WHITE, COLOUR_L_BLUE},
	{COLOUR_SLATE, COLOUR_WHITE, COLOUR_L_DARK},
	{COLOUR_ORANGE, COLOUR_YELLOW, COLOUR_L_RED},
	{COLOUR_RED, COLOUR_L_RED, COLOUR_L_PINK},
	{COLOUR_GREEN, COLOUR_L_GREEN, COLOUR_L_TEAL},
	{COLOUR_BLUE, COLOUR_L_BLUE, COLOUR_SLATE},
	{COLOUR_UMBER, COLOUR_L_UMBER, COLOUR_MUSTARD},
	{COLOUR_L_DARK, COLOUR_SLATE, COLOUR_L_VIOLET},
	{COLOUR_WHITE, COLOUR_SLATE, COLOUR_L_WHITE},
	{COLOUR_L_PURPLE, COLOUR_PURPLE, COLOUR_L_VIOLET},
	{COLOUR_YELLOW, COLOUR_L_YELLOW, COLOUR_MUSTARD},
	{COLOUR_L_RED, COLOUR_RED, COLOUR_L_PINK},
	{COLOUR_L_GREEN, COLOUR_L_TEAL, COLOUR_GREEN},
	{COLOUR_L_BLUE, COLOUR_DEEP_L_BLUE, COLOUR_BLUE_SLATE},
	{COLOUR_L_UMBER, COLOUR_UMBER, COLOUR_MUD},
	{COLOUR_PURPLE, COLOUR_VIOLET, COLOUR_MAGENTA},
	{COLOUR_VIOLET, COLOUR_L_VIOLET, COLOUR_MAGENTA},
	{COLOUR_TEAL, COLOUR_L_TEAL, COLOUR_L_GREEN},
	{COLOUR_MUD, COLOUR_YELLOW, COLOUR_UMBER},
	{COLOUR_L_YELLOW, COLOUR_WHITE, COLOUR_L_UMBER},
	{COLOUR_MAGENTA, COLOUR_L_PINK, COLOUR_L_RED},
	{COLOUR_L_TEAL, COLOUR_L_WHITE, COLOUR_TEAL},
	{COLOUR_L_VIOLET, COLOUR_L_PURPLE, COLOUR_VIOLET},
	{COLOUR_L_PINK, COLOUR_L_RED, COLOUR_L_WHITE},
	{COLOUR_MUSTARD, COLOUR_YELLOW, COLOUR_UMBER},
	{COLOUR_BLUE_SLATE, COLOUR_BLUE, COLOUR_SLATE},
	{COLOUR_DEEP_L_BLUE, COLOUR_L_BLUE, COLOUR_BLUE},
};

static byte get_flicker(byte a)
{
	switch(flicker % 3)
	{
		case 1: return color_flicker[a][1];
		case 2: return color_flicker[a][2];
	}
	return a;
}

/**
 * This animates monsters and/or items as necessary.
 */
static void do_animation(void)
{
	int i;

	for (i = 1; i < cave_monster_max(cave); i++) {
		byte attr;
		struct monster *mon = cave_monster(cave, i);

		if (!mon || !mon->race || !mflag_has(mon->mflag, MFLAG_VISIBLE))
			continue;
		else if (rf_has(mon->race->flags, RF_ATTR_MULTI))
			attr = randint1(BASIC_COLORS - 1);
		else if (rf_has(mon->race->flags, RF_ATTR_FLICKER))
			attr = get_flicker(monster_x_attr[mon->race->ridx]);
		else
			continue;

		mon->attr = attr;
		player->upkeep->redraw |= (PR_MAP | PR_MONLIST);
	}

	flicker++;
}

/**
 * Set animations to allowed
 */
void allow_animations(void)
{
	animations_allowed = true;
}

/**
 * Set animations to disallowed
 */
void disallow_animations(void)
{
	animations_allowed = false;
}

/**
 * Update animations on request
 */
static void animate(game_event_type type, game_event_data *data, void *user)
{
	do_animation();
}

/**
 * This is used when the user is idle to allow for simple animations.
 * Currently the only thing it really does is animate shimmering monsters.
 */
void idle_update(void)
{
	if (!animations_allowed) return;
	if (msg_flag) return;
	if (!character_dungeon) return;
	if (!OPT(player, animate_flicker) || (use_graphics != GRAPHICS_NONE))
		return;

	/* Animate and redraw if necessary */
	do_animation();
	redraw_stuff(player);

	/* Refresh the main screen */
	Term_fresh();
}


/**
 * Find the attr/char pair to use for a spell effect
 *
 * It is moving (or has moved) from (x, y) to (nx, ny); if the distance is not
 * "one", we (may) return "*".
 */
static void bolt_pict(int y, int x, int ny, int nx, int typ, byte *a,
					  wchar_t *c)
{
	int motion;

	/* Convert co-ordinates into motion */
	if ((ny == y) && (nx == x))
		motion = BOLT_NO_MOTION;
	else if (nx == x)
		motion = BOLT_0;
	else if ((ny-y) == (x-nx))
		motion = BOLT_45;
	else if (ny == y)
		motion = BOLT_90;
	else if ((ny-y) == (nx-x))
		motion = BOLT_135;
	else
		motion = BOLT_NO_MOTION;

	/* Decide on output char */
	if (use_graphics == GRAPHICS_NONE) {
		/* ASCII is simple */
		wchar_t chars[] = L"*|/-\\";

		*c = chars[motion];
		*a = projections[typ].color;
	} else {
		*a = proj_to_attr[typ][motion];
		*c = proj_to_char[typ][motion];
	}
}

/**
 * Draw an explosion
 */
static void display_explosion(game_event_type type, game_event_data *data,
							  void *user)
{
	bool new_radius = false;
	bool drawn = false;
	int i, y, x;
	int msec = player->opts.delay_factor;
	int proj_type = data->explosion.proj_type;
	int num_grids = data->explosion.num_grids;
	int *distance_to_grid = data->explosion.distance_to_grid;
	bool drawing = data->explosion.drawing;
	bool *player_sees_grid = data->explosion.player_sees_grid;
	struct loc *blast_grid = data->explosion.blast_grid;
	struct loc centre = data->explosion.centre;

	/* Draw the blast from inside out */
	for (i = 0; i < num_grids; i++) {
		/* Extract the location */
		y = blast_grid[i].y;
		x = blast_grid[i].x;

		/* Only do visuals if the player can see the blast */
		if (player_sees_grid[i]) {
			byte a;
			wchar_t c;

			drawn = true;

			/* Obtain the explosion pict */
			bolt_pict(y, x, y, x, proj_type, &a, &c);

			/* Just display the pict, ignoring what was under it */
			print_rel(c, a, y, x);
		}

		/* Center the cursor to stop it tracking the blast grids  */
		move_cursor_relative(centre.y, centre.x);

		/* Check for new radius, taking care not to overrun array */
		if (i == num_grids - 1)
			new_radius = true;
		else if (distance_to_grid[i + 1] > distance_to_grid[i])
			new_radius = true;

		/* We have all the grids at the current radius, so draw it */
		if (new_radius) {
			/* Flush all the grids at this radius */
			Term_fresh();
			if (player->upkeep->redraw)
				redraw_stuff(player);

			/* Delay to show this radius appearing */
			if (drawn || drawing) {
				Term_xtra(TERM_XTRA_DELAY, msec);
			}

			new_radius = false;
		}
	}

	/* Erase and flush */
	if (drawn) {
		/* Erase the explosion drawn above */
		for (i = 0; i < num_grids; i++) {
			/* Extract the location */
			y = blast_grid[i].y;
			x = blast_grid[i].x;

			/* Erase visible, valid grids */
			if (player_sees_grid[i])
				event_signal_point(EVENT_MAP, x, y);
		}

		/* Center the cursor */
		move_cursor_relative(centre.y, centre.x);

		/* Flush the explosion */
		Term_fresh();
		if (player->upkeep->redraw)
			redraw_stuff(player);
	}
}

/**
 * Draw a moving spell effect (bolt or beam)
 */
static void display_bolt(game_event_type type, game_event_data *data,
						 void *user)
{
	int msec = player->opts.delay_factor;
	int proj_type = data->bolt.proj_type;
	bool drawing = data->bolt.drawing;
	bool seen = data->bolt.seen;
	bool beam = data->bolt.beam;
	int oy = data->bolt.oy;
	int ox = data->bolt.ox;
	int y = data->bolt.y;
	int x = data->bolt.x;

	/* Only do visuals if the player can "see" the bolt */
	if (seen) {
		byte a;
		wchar_t c;

		/* Obtain the bolt pict */
		bolt_pict(oy, ox, y, x, proj_type, &a, &c);

		/* Visual effects */
		print_rel(c, a, y, x);
		move_cursor_relative(y, x);
		Term_fresh();
		if (player->upkeep->redraw)
			redraw_stuff(player);
		Term_xtra(TERM_XTRA_DELAY, msec);
		event_signal_point(EVENT_MAP, x, y);
		Term_fresh();
		if (player->upkeep->redraw)
			redraw_stuff(player);

		/* Display "beam" grids */
		if (beam) {

			/* Obtain the explosion pict */
			bolt_pict(y, x, y, x, proj_type, &a, &c);

			/* Visual effects */
			print_rel(c, a, y, x);
		}
	} else if (drawing) {
		/* Delay for consistency */
		Term_xtra(TERM_XTRA_DELAY, msec);
	}
}

/**
 * Draw a moving missile
 */
static void display_missile(game_event_type type, game_event_data *data,
							void *user)
{
	int msec = player->opts.delay_factor;
	struct object *obj = data->missile.obj;
	bool seen = data->missile.seen;
	int y = data->missile.y;
	int x = data->missile.x;

	/* Only do visuals if the player can "see" the missile */
	if (seen) {
		print_rel(object_char(obj), object_attr(obj), y, x);
		move_cursor_relative(y, x);

		Term_fresh();
		if (player->upkeep->redraw) redraw_stuff(player);

		Term_xtra(TERM_XTRA_DELAY, msec);
		event_signal_point(EVENT_MAP, x, y);

		Term_fresh();
		if (player->upkeep->redraw) redraw_stuff(player);
	}
}

/**
 * ------------------------------------------------------------------------
 * Subwindow displays
 * ------------------------------------------------------------------------ */

/**
 * true when we're supposed to display the equipment in the inventory 
 * window, or vice-versa.
 */
static bool flip_inven;

static void update_inven_subwindow(game_event_type type, game_event_data *data,
				       void *user)
{
	term *old = Term;
	term *inv_term = user;

	/* Activate */
	Term_activate(inv_term);

	if (!flip_inven)
		show_inven(OLIST_WINDOW | OLIST_WEIGHT | OLIST_QUIVER, NULL);
	else
		show_equip(OLIST_WINDOW | OLIST_WEIGHT, NULL);

	Term_fresh();
	
	/* Restore */
	Term_activate(old);
}

static void update_equip_subwindow(game_event_type type, game_event_data *data,
				   void *user)
{
	term *old = Term;
	term *inv_term = user;

	/* Activate */
	Term_activate(inv_term);

	if (!flip_inven)
		show_equip(OLIST_WINDOW | OLIST_WEIGHT, NULL);
	else
		show_inven(OLIST_WINDOW | OLIST_WEIGHT | OLIST_QUIVER, NULL);

	Term_fresh();
	
	/* Restore */
	Term_activate(old);
}

/**
 * Flip "inven" and "equip" in any sub-windows
 */
void toggle_inven_equip(void)
{
	term *old = Term;
	int i;

	/* Change the actual setting */
	flip_inven = !flip_inven;

	/* Redraw any subwindows showing the inventory/equipment lists */
	for (i = 0; i < ANGBAND_TERM_MAX; i++) {
		Term_activate(angband_term[i]); 

		if (window_flag[i] & PW_INVEN) {
			if (!flip_inven)
				show_inven(OLIST_WINDOW | OLIST_WEIGHT | OLIST_QUIVER, NULL);
			else
				show_equip(OLIST_WINDOW | OLIST_WEIGHT, NULL);
			
			Term_fresh();
		} else if (window_flag[i] & PW_EQUIP) {
			if (!flip_inven)
				show_equip(OLIST_WINDOW | OLIST_WEIGHT, NULL);
			else
				show_inven(OLIST_WINDOW | OLIST_WEIGHT | OLIST_QUIVER, NULL);
			
			Term_fresh();
		}
	}

	Term_activate(old);
}

static void update_itemlist_subwindow(game_event_type type,
									  game_event_data *data, void *user)
{
	term *old = Term;
	term *inv_term = user;

	/* Activate */
	Term_activate(inv_term);

    clear_from(0);
    object_list_show_subwindow(Term->hgt, Term->wid);
	Term_fresh();
	
	/* Restore */
	Term_activate(old);
}

static void update_monlist_subwindow(game_event_type type,
									 game_event_data *data, void *user)
{
	term *old = Term;
	term *inv_term = user;

	/* Activate */
	Term_activate(inv_term);

	clear_from(0);
	monster_list_show_subwindow(Term->hgt, Term->wid);
	Term_fresh();
	
	/* Restore */
	Term_activate(old);
}


static void update_monster_subwindow(game_event_type type,
									 game_event_data *data, void *user)
{
	term *old = Term;
	term *inv_term = user;

	/* Activate */
	Term_activate(inv_term);

	/* Display monster race info */
	if (player->upkeep->monster_race)
		lore_show_subwindow(player->upkeep->monster_race, 
							get_lore(player->upkeep->monster_race));

	Term_fresh();
	
	/* Restore */
	Term_activate(old);
}


static void update_object_subwindow(game_event_type type,
									game_event_data *data, void *user)
{
	term *old = Term;
	term *inv_term = user;
	
	/* Activate */
	Term_activate(inv_term);
	
	if (player->upkeep->object != NULL)
		display_object_recall(player->upkeep->object);
	else if (player->upkeep->object_kind)
		display_object_kind_recall(player->upkeep->object_kind);
	Term_fresh();
	
	/* Restore */
	Term_activate(old);
}


static void update_messages_subwindow(game_event_type type,
									  game_event_data *data, void *user)
{
	term *old = Term;
	term *inv_term = user;

	int i;
	int w, h;
	int x, y;

	const char *msg;

	/* Activate */
	Term_activate(inv_term);

	/* Get size */
	Term_get_size(&w, &h);

	/* Dump messages */
	for (i = 0; i < h; i++) {
		byte color = message_color(i);
		u16b count = message_count(i);
		const char *str = message_str(i);

		if (count == 1)
			msg = str;
		else if (count == 0)
			msg = " ";
		else
			msg = format("%s <%dx>", str, count);

		Term_putstr(0, (h - 1) - i, -1, color, msg);


		/* Cursor */
		Term_locate(&x, &y);

		/* Clear to end of line */
		Term_erase(x, y, 255);
	}

	Term_fresh();
	
	/* Restore */
	Term_activate(old);
}

static struct minimap_flags
{
	int win_idx;
	bool needs_redraw;
} minimap_data[ANGBAND_TERM_MAX];

static void update_minimap_subwindow(game_event_type type,
	game_event_data *data, void *user)
{
	struct minimap_flags *flags = user;

	if (player_resting_count(player) || player->upkeep->running) return;

	if (type == EVENT_END) {
		term *old = Term;
		term *t = angband_term[flags->win_idx];
		
		/* Activate */
		Term_activate(t);

		/* If whole-map redraw, clear window first. */
		if (flags->needs_redraw)
			Term_clear();

		/* Redraw map */
		display_map(NULL, NULL);
		Term_fresh();
		
		/* Restore */
		Term_activate(old);

		flags->needs_redraw = false;
	} else if (type == EVENT_DUNGEONLEVEL) {
		/* XXX map_height and map_width need to be kept in sync with
		 * display_map() */
		term *t = angband_term[flags->win_idx];
		int map_height = t->hgt - 2;
		int map_width = t->wid - 2;

		/* Clear the entire term if the new map isn't going to fit the
		 * entire thing */
		if (cave->height <= map_height || cave->width <= map_width) {
			flags->needs_redraw = true;
		}
	}
}


/**
 * Display player in sub-windows (mode 0)
 */
static void update_player0_subwindow(game_event_type type,
									 game_event_data *data, void *user)
{
	term *old = Term;
	term *inv_term = user;

	/* Activate */
	Term_activate(inv_term);

	/* Display flags */
	display_player(0);

	Term_fresh();
	
	/* Restore */
	Term_activate(old);
}

/**
 * Display player in sub-windows (mode 1)
 */
static void update_player1_subwindow(game_event_type type,
									 game_event_data *data, void *user)
{
	term *old = Term;
	term *inv_term = user;

	/* Activate */
	Term_activate(inv_term);

	/* Display flags */
	display_player(1);

	Term_fresh();
	
	/* Restore */
	Term_activate(old);
}


/**
 * Display the left-hand-side of the main term, in more compact fashion.
 */
static void update_player_compact_subwindow(game_event_type type,
											game_event_data *data, void *user)
{
	int row = 0;
	int col = 0;
	int i;

	term *old = Term;
	term *inv_term = user;

	/* Activate */
	Term_activate(inv_term);

	/* Race and Class */
	prt_field(player->race->name, row++, col);
	prt_field(player->class->name, row++, col);

	/* Title */
	prt_title(row++, col);

	/* Level/Experience */
	prt_level(row++, col);
	prt_exp(row++, col);

	/* Gold */
	prt_gold(row++, col);

	/* Equippy chars */
	prt_equippy(row++, col);

	/* All Stats */
	for (i = 0; i < STAT_MAX; i++) prt_stat(i, row++, col);

	/* Empty row */
	row++;

	/* Armor */
	prt_ac(row++, col);

	/* Hitpoints */
	prt_hp(row++, col);

	/* Spellpoints */
	prt_sp(row++, col);

	/* Monster health */
	prt_health(row++, col);

	Term_fresh();
	
	/* Restore */
	Term_activate(old);
}


static void flush_subwindow(game_event_type type, game_event_data *data,
							void *user)
{
	term *old = Term;
	term *t = user;

	/* Activate */
	Term_activate(t);

	Term_fresh();
	
	/* Restore */
	Term_activate(old);
}

/**
 * Certain "screens" always use the main screen, including News, Birth,
 * Dungeon, Tomb-stone, High-scores, Macros, Colors, Visuals, Options.
 *
 * Later, special flags may allow sub-windows to "steal" stuff from the
 * main window, including File dump (help), File dump (artifacts, uniques),
 * Character screen, Small scale map, Previous Messages, Store screen, etc.
 */
const char *window_flag_desc[32] =
{
	"Display inven/equip",
	"Display equip/inven",
	"Display player (basic)",
	"Display player (extra)",
	"Display player (compact)",
	"Display map view",
	"Display messages",
	"Display overhead view",
	"Display monster recall",
	"Display object recall",
	"Display monster list",
	"Display status",
	"Display item list",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

static void subwindow_flag_changed(int win_idx, u32b flag, bool new_state)
{
	void (*register_or_deregister)(game_event_type type, game_event_handler *fn,
								   void *user);
	void (*set_register_or_deregister)(game_event_type *type, size_t n_events,
									   game_event_handler *fn, void *user);

	/* Decide whether to register or deregister an evenrt handler */
	if (new_state == false) {
		register_or_deregister = event_remove_handler;
		set_register_or_deregister = event_remove_handler_set;
	} else {
		register_or_deregister = event_add_handler;
		set_register_or_deregister = event_add_handler_set;
	}

	switch (flag)
	{
		case PW_INVEN:
		{
			register_or_deregister(EVENT_INVENTORY,
					       update_inven_subwindow,
					       angband_term[win_idx]);
			break;
		}

		case PW_EQUIP:
		{
			register_or_deregister(EVENT_EQUIPMENT,
					       update_equip_subwindow,
					       angband_term[win_idx]);
			break;
		}

		case PW_PLAYER_0:
		{
			set_register_or_deregister(player_events, 
						   N_ELEMENTS(player_events),
						   update_player0_subwindow,
						   angband_term[win_idx]);
			break;
		}

		case PW_PLAYER_1:
		{
			set_register_or_deregister(player_events, 
						   N_ELEMENTS(player_events),
						   update_player1_subwindow,
						   angband_term[win_idx]);
			break;
		}

		case PW_PLAYER_2:
		{
			set_register_or_deregister(player_events, 
						   N_ELEMENTS(player_events),
						   update_player_compact_subwindow,
						   angband_term[win_idx]);
			break;
		}

		case PW_MAP:
		{
			register_or_deregister(EVENT_MAP,
					       update_maps,
					       angband_term[win_idx]);

			register_or_deregister(EVENT_END,
					       flush_subwindow,
					       angband_term[win_idx]);
			break;
		}

		case PW_MESSAGE:
		{
			register_or_deregister(EVENT_MESSAGE,
					       update_messages_subwindow,
					       angband_term[win_idx]);
			break;
		}

		case PW_OVERHEAD:
		{
			minimap_data[win_idx].win_idx = win_idx;

			register_or_deregister(EVENT_MAP,
					       update_minimap_subwindow,
					       &minimap_data[win_idx]);

			register_or_deregister(EVENT_DUNGEONLEVEL, update_minimap_subwindow,
								   &minimap_data[win_idx]);

			register_or_deregister(EVENT_END,
					       update_minimap_subwindow,
					       &minimap_data[win_idx]);
			break;
		}

		case PW_MONSTER:
		{
			register_or_deregister(EVENT_MONSTERTARGET,
					       update_monster_subwindow,
					       angband_term[win_idx]);
			break;
		}

		case PW_OBJECT:
		{
			register_or_deregister(EVENT_OBJECTTARGET,
						   update_object_subwindow,
						   angband_term[win_idx]);
			break;
		}

		case PW_MONLIST:
		{
			register_or_deregister(EVENT_MONSTERLIST,
					       update_monlist_subwindow,
					       angband_term[win_idx]);
			break;
		}

		case PW_ITEMLIST:
		{
			register_or_deregister(EVENT_ITEMLIST,
						   update_itemlist_subwindow,
						   angband_term[win_idx]);
			break;
		}
	}
}


/**
 * Set the flags for one Term, calling "subwindow_flag_changed" with each flag
 * that has changed setting so that it can do any housekeeping to do with 
 * displaying the new thing or no longer displaying the old one.
 */
static void subwindow_set_flags(int win_idx, u32b new_flags)
{
	term *old = Term;
	int i;

	/* Deal with the changed flags by seeing what's changed */
	for (i = 0; i < 32; i++)
		/* Only process valid flags */
		if (window_flag_desc[i])
			if ((new_flags & (1L << i)) != (window_flag[win_idx] & (1L << i)))
				subwindow_flag_changed(win_idx, (1L << i),
									   (new_flags & (1L << i)) != 0);

	/* Store the new flags */
	window_flag[win_idx] = new_flags;
	
	/* Activate */
	Term_activate(angband_term[win_idx]);
	
	/* Erase */
	Term_clear();
	
	/* Refresh */
	Term_fresh();
			
	/* Restore */
	Term_activate(old);
}

/**
 * Called with an array of the new flags for all the subwindows, in order
 * to set them to the new values, with a chance to perform housekeeping.
 */
void subwindows_set_flags(u32b *new_flags, size_t n_subwindows)
{
	size_t j;

	for (j = 0; j < n_subwindows; j++) {
		/* Dead window */
		if (!angband_term[j]) continue;

		/* Ignore non-changes */
		if (window_flag[j] != new_flags[j])
			subwindow_set_flags(j, new_flags[j]);
	}
}

/**
 * ------------------------------------------------------------------------
 * Showing and updating the splash screen.
 * ------------------------------------------------------------------------ */
/**
 * Explain a broken "lib" folder and quit (see below).
 */
static void init_angband_aux(const char *why)
{
	quit_fmt("%s\n\n%s", why,
	         "The 'lib' directory is probably missing or broken.\n"
	         "Perhaps the archive was not extracted correctly.\n"
	         "See the 'readme.txt' file for more information.");
}

/*
 * Take notes on line 23
 */
static void splashscreen_note(game_event_type type, game_event_data *data,
							  void *user)
{
	if (data->message.type == MSG_BIRTH) {
		static int y = 2;

		/* Draw the message */
		prt(data->message.msg, y, 0);
		pause_line(Term);

		/* Advance one line (wrap if needed) */
		if (++y >= 24) y = 2;
	} else {
		char *s = format("[%s]", data->message.msg);
		Term_erase(0, (Term->hgt - 23) / 5 + 23, 255);
		Term_putstr((Term->wid - strlen(s)) / 2, (Term->hgt - 23) / 5 + 23, -1,
					COLOUR_WHITE, s);
	}

	Term_fresh();
}

static void show_splashscreen(game_event_type type, game_event_data *data,
							  void *user)
{
	ang_file *fp;

	char buf[1024];

	/* Verify the "news" file */
	path_build(buf, sizeof(buf), ANGBAND_DIR_SCREENS, "news.txt");
	if (!file_exists(buf)) {
		char why[1024];

		/* Crash and burn */
		strnfmt(why, sizeof(why), "Cannot access the '%s' file!", buf);
		init_angband_aux(why);
	}


	/* Prepare to display the "news" file */
	Term_clear();

	/* Open the News file */
	path_build(buf, sizeof(buf), ANGBAND_DIR_SCREENS, "news.txt");
	fp = file_open(buf, MODE_READ, FTYPE_TEXT);

	text_out_hook = text_out_to_screen;

	/* Dump */
	if (fp) {
		/* Centre the splashscreen - assume news.txt has width 80, height 23 */
		text_out_indent = (Term->wid - 80) / 2;
		Term_gotoxy(0, (Term->hgt - 23) / 5);

		/* Dump the file to the screen */
		while (file_getl(fp, buf, sizeof(buf))) {
			char *version_marker = strstr(buf, "$VERSION");
			if (version_marker) {
				ptrdiff_t pos = version_marker - buf;
				strnfmt(version_marker, sizeof(buf) - pos, "%-8s", buildver);
			}

			text_out_e("%s", buf);
			text_out("\n");
		}

		text_out_indent = 0;
		file_close(fp);
	}

	/* Flush it */
	Term_fresh();
}


/**
 * ------------------------------------------------------------------------
 * Visual updates betweeen player turns.
 * ------------------------------------------------------------------------ */
static void refresh(game_event_type type, game_event_data *data, void *user)
{
	/* Place cursor on player/target */
	if (OPT(player, show_target) && target_sighted()) {
		int col, row;
		target_get(&col, &row);
		move_cursor_relative(row, col);
	}

	Term_fresh();
}

static void repeated_command_display(game_event_type type,
									 game_event_data *data, void *user)
{
	/* Assume messages were seen */
	msg_flag = false;

	/* Clear the top line */
	prt("", 0, 0);
}

/**
 * Housekeeping on arriving on a new level
 */
static void new_level_display_update(game_event_type type,
									 game_event_data *data, void *user)
{
	/* Hack -- enforce illegal panel */
	Term->offset_y = z_info->dungeon_hgt;
	Term->offset_x = z_info->dungeon_wid;

	/* If autosave is pending, do it now. */
	if (player->upkeep->autosave) {
		save_game();
		player->upkeep->autosave = false;
	}

	/* Choose panel */
	verify_panel();

	/* Hack -- Invoke partial update mode */
	player->upkeep->only_partial = true;

	/* Clear */
	Term_clear();

	/* Update stuff */
	player->upkeep->update |= (PU_BONUS | PU_HP | PU_SPELLS);

	/* Calculate torch radius */
	player->upkeep->update |= (PU_TORCH);

	/* Update stuff */
	update_stuff(player);

	/* Fully update the visuals (and monster distances) */
	player->upkeep->update |= (PU_UPDATE_VIEW | PU_DISTANCE);

	/* Fully update the flow */
	player->upkeep->update |= (PU_FORGET_FLOW | PU_UPDATE_FLOW);

	/* Redraw dungeon */
	player->upkeep->redraw |= (PR_BASIC | PR_EXTRA | PR_MAP);

	/* Redraw "statusy" things */
	player->upkeep->redraw |= (PR_INVEN | PR_EQUIP | PR_MONSTER | PR_MONLIST | PR_ITEMLIST);

	/* Because changing levels doesn't take a turn and PR_MONLIST might not be
	 * set for a few game turns, manually force an update on level change. */
	monster_list_force_subwindow_update();

	/* Update stuff */
	update_stuff(player);

	/* Redraw stuff */
	redraw_stuff(player);

	/* Hack -- Kill partial update mode */
	player->upkeep->only_partial = false;

	/* Refresh */
	Term_fresh();
}


/**
 * ------------------------------------------------------------------------
 * Temporary (hopefully) hackish solutions.
 * ------------------------------------------------------------------------ */
static void cheat_death(game_event_type type, game_event_data *data, void *user)
{
	msg("You invoke wizard mode and cheat death.");
	event_signal(EVENT_MESSAGE_FLUSH);

	wiz_cheat_death();
}

static void check_panel(game_event_type type, game_event_data *data, void *user)
{
	verify_panel();
}

static void see_floor_items(game_event_type type, game_event_data *data,
							void *user)
{
	int floor_max = z_info->floor_size;
	struct object **floor_list = mem_zalloc(floor_max * sizeof(*floor_list));
	int floor_num = 0;
	bool blind = ((player->timed[TMD_BLIND]) || (no_light()));

	const char *p = "see";
	bool can_pickup = false;
	int i;

	/* Scan all visible, sensed objects in the grid */
	floor_num = scan_floor(floor_list, floor_max,
						   OFLOOR_SENSE | OFLOOR_VISIBLE, NULL);
	if (floor_num == 0) {
		mem_free(floor_list);
		return;
	}

	/* Can we pick any up? */
	for (i = 0; i < floor_num; i++)
	    if (inven_carry_okay(floor_list[i]))
			can_pickup = true;

	/* One object */
	if (floor_num == 1) {
		/* Get the object */
		struct object *obj = floor_list[0];
		char o_name[80];

		if (!can_pickup)
			p = "have no room for";
		else if (blind)
			p = "feel";

		/* Describe the object.  Less detail if blind. */
		if (blind)
			object_desc(o_name, sizeof(o_name), obj, ODESC_PREFIX | ODESC_BASE);
		else
			object_desc(o_name, sizeof(o_name), obj, ODESC_PREFIX | ODESC_FULL);

		/* Message */
		event_signal(EVENT_MESSAGE_FLUSH);
		msg("You %s %s.", p, o_name);
	} else {
		ui_event e;

		if (!can_pickup)
			p = "have no room for the following objects";
		else if (blind)
			p = "feel something on the floor";

		/* Display objects on the floor */
		screen_save();
		show_floor(floor_list, floor_num, OLIST_WEIGHT, NULL);
		prt(format("You %s: ", p), 0, 0);

		/* Wait for it.  Use key as next command. */
		e = inkey_ex();
		Term_event_push(&e);

		/* Restore screen */
		screen_load();
	}

	mem_free(floor_list);
}

/**
 * ------------------------------------------------------------------------
 * Initialising
 * ------------------------------------------------------------------------ */

/**
 * Process the user pref files relevant to a newly loaded character
 */
static void process_character_pref_files(void)
{
	bool found;
	char buf[1024];

	/* Process the "window.prf" file */
	process_pref_file("window.prf", true, true);

	/* Process the "user.prf" file */
	process_pref_file("user.prf", true, true);

	/* Get the filesystem-safe name and append .prf */
	player_safe_name(buf, sizeof(buf), player->full_name, true);
	my_strcat(buf, ".prf", sizeof(buf));

	found = process_pref_file(buf, true, true);

    /* Try pref file using savefile name if we fail using character name */
    if (!found) {
		int filename_index = path_filename_index(savefile);
		char filename[128];

		my_strcpy(filename, &savefile[filename_index], sizeof(filename));
		strnfmt(buf, sizeof(buf), "%s.prf", filename);
		process_pref_file(buf, true, true);
    }
}


static void ui_enter_init(game_event_type type, game_event_data *data,
						  void *user)
{
	show_splashscreen(type, data, user);

	/* Set up our splashscreen handlers */
	event_add_handler(EVENT_INITSTATUS, splashscreen_note, NULL);
}

static void ui_leave_init(game_event_type type, game_event_data *data,
						  void *user)
{
	/* Reset visuals, then load prefs */
	reset_visuals(true);
	process_character_pref_files();

	/* Remove our splashscreen handlers */
	event_remove_handler(EVENT_INITSTATUS, splashscreen_note, NULL);

	/* Flash a message */
	prt("Please wait...", 0, 0);

	/* Flush the message */
	Term_fresh();
}

static void ui_enter_world(game_event_type type, game_event_data *data,
						  void *user)
{
	/* Allow big cursor */
	smlcurs = false;

	/* Redraw stuff */
	player->upkeep->redraw |= (PR_INVEN | PR_EQUIP | PR_MONSTER | PR_MESSAGE);
	redraw_stuff(player);

	/* React to changes */
	Term_xtra(TERM_XTRA_REACT, 0);

	/* Because of the "flexible" sidebar, all these things trigger
	   the same function. */
	event_add_handler_set(player_events, N_ELEMENTS(player_events),
			      update_sidebar, NULL);

	/* The flexible statusbar has similar requirements, so is
	   also trigger by a large set of events. */
	event_add_handler_set(statusline_events, N_ELEMENTS(statusline_events),
			      update_statusline, NULL);

	/* Player HP can optionally change the colour of the '@' now. */
	event_add_handler(EVENT_HP, hp_colour_change, NULL);

	/* Simplest way to keep the map up to date - will do for now */
	event_add_handler(EVENT_MAP, update_maps, angband_term[0]);
#ifdef MAP_DEBUG
	event_add_handler(EVENT_MAP, trace_map_updates, angband_term[0]);
#endif

	/* Check if the panel should shift when the player's moved */
	event_add_handler(EVENT_PLAYERMOVED, check_panel, NULL);

	/* Take note of what's on the floor */
	event_add_handler(EVENT_SEEFLOOR, see_floor_items, NULL);

	/* Enter a store */
	event_add_handler(EVENT_ENTER_STORE, enter_store, NULL);

	/* Display an explosion */
	event_add_handler(EVENT_EXPLOSION, display_explosion, NULL);

	/* Display a bolt spell */
	event_add_handler(EVENT_BOLT, display_bolt, NULL);

	/* Display a physical missile */
	event_add_handler(EVENT_MISSILE, display_missile, NULL);

	/* Check to see if the player has tried to cancel game processing */
	event_add_handler(EVENT_CHECK_INTERRUPT, check_for_player_interrupt, NULL);

	/* Refresh the screen and put the cursor in the appropriate place */
	event_add_handler(EVENT_REFRESH, refresh, NULL);

	/* Do the visual updates required on a new dungeon level */
	event_add_handler(EVENT_NEW_LEVEL_DISPLAY, new_level_display_update, NULL);

	/* Automatically clear messages while the game is repeating commands */
	event_add_handler(EVENT_COMMAND_REPEAT, repeated_command_display, NULL);

	/* Do animations (e.g. monster colour changes) */
	event_add_handler(EVENT_ANIMATE, animate, NULL);

	/* Allow the player to cheat death, if appropriate */
	event_add_handler(EVENT_CHEAT_DEATH, cheat_death, NULL);

	/* Hack -- Decrease "icky" depth */
	screen_save_depth--;
}

static void ui_leave_world(game_event_type type, game_event_data *data,
						  void *user)
{
	/* Disallow big cursor */
	smlcurs = true;

	/* Because of the "flexible" sidebar, all these things trigger
	   the same function. */
	event_remove_handler_set(player_events, N_ELEMENTS(player_events),
			      update_sidebar, NULL);

	/* The flexible statusbar has similar requirements, so is
	   also trigger by a large set of events. */
	event_remove_handler_set(statusline_events, N_ELEMENTS(statusline_events),
			      update_statusline, NULL);

	/* Player HP can optionally change the colour of the '@' now. */
	event_remove_handler(EVENT_HP, hp_colour_change, NULL);

	/* Simplest way to keep the map up to date - will do for now */
	event_remove_handler(EVENT_MAP, update_maps, angband_term[0]);
#ifdef MAP_DEBUG
	event_remove_handler(EVENT_MAP, trace_map_updates, angband_term[0]);
#endif

	/* Check if the panel should shift when the player's moved */
	event_remove_handler(EVENT_PLAYERMOVED, check_panel, NULL);

	/* Take note of what's on the floor */
	event_remove_handler(EVENT_SEEFLOOR, see_floor_items, NULL);

	/* Display an explosion */
	event_remove_handler(EVENT_EXPLOSION, display_explosion, NULL);

	/* Display a bolt spell */
	event_remove_handler(EVENT_BOLT, display_bolt, NULL);

	/* Display a physical missile */
	event_remove_handler(EVENT_MISSILE, display_missile, NULL);

	/* Check to see if the player has tried to cancel game processing */
	event_remove_handler(EVENT_CHECK_INTERRUPT, check_for_player_interrupt, NULL);

	/* Refresh the screen and put the cursor in the appropriate place */
	event_remove_handler(EVENT_REFRESH, refresh, NULL);

	/* Do the visual updates required on a new dungeon level */
	event_remove_handler(EVENT_NEW_LEVEL_DISPLAY, new_level_display_update, NULL);

	/* Automatically clear messages while the game is repeating commands */
	event_remove_handler(EVENT_COMMAND_REPEAT, repeated_command_display, NULL);

	/* Do animations (e.g. monster colour changes) */
	event_remove_handler(EVENT_ANIMATE, animate, NULL);

	/* Allow the player to cheat death, if appropriate */
	event_remove_handler(EVENT_CHEAT_DEATH, cheat_death, NULL);

	/* Prepare to interact with a store */
	event_add_handler(EVENT_USE_STORE, use_store, NULL);

	/* If we've gone into a store, we need to know how to leave */
	event_add_handler(EVENT_LEAVE_STORE, leave_store, NULL);

	/* Hack -- Increase "icky" depth */
	screen_save_depth++;
}

static void ui_enter_game(game_event_type type, game_event_data *data,
						  void *user)
{
	/* Display a message to the player */
	event_add_handler(EVENT_MESSAGE, display_message, NULL);

	/* Display a message and make a noise to the player */
	event_add_handler(EVENT_BELL, bell_message, NULL);

	/* Tell the UI to ignore all pending input */
	event_add_handler(EVENT_INPUT_FLUSH, flush, NULL);

	/* Print all waiting messages */
	event_add_handler(EVENT_MESSAGE_FLUSH, message_flush, NULL);
}

static void ui_leave_game(game_event_type type, game_event_data *data,
						  void *user)
{
	/* Display a message to the player */
	event_remove_handler(EVENT_MESSAGE, display_message, NULL);

	/* Display a message and make a noise to the player */
	event_remove_handler(EVENT_BELL, bell_message, NULL);

	/* Tell the UI to ignore all pending input */
	event_remove_handler(EVENT_INPUT_FLUSH, flush, NULL);

	/* Print all waiting messages */
	event_remove_handler(EVENT_MESSAGE_FLUSH, message_flush, NULL);
}

void init_display(void)
{
	event_add_handler(EVENT_ENTER_INIT, ui_enter_init, NULL);
	event_add_handler(EVENT_LEAVE_INIT, ui_leave_init, NULL);

	event_add_handler(EVENT_ENTER_GAME, ui_enter_game, NULL);
	event_add_handler(EVENT_LEAVE_GAME, ui_leave_game, NULL);

	event_add_handler(EVENT_ENTER_WORLD, ui_enter_world, NULL);
	event_add_handler(EVENT_LEAVE_WORLD, ui_leave_world, NULL);

	ui_init_birthstate_handlers();
}
