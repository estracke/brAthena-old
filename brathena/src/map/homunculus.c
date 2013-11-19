/****************************************************************************!
*                _           _   _   _                                       *    
*               | |__  _ __ / \ | |_| |__   ___ _ __   __ _                  *  
*               | '_ \| '__/ _ \| __| '_ \ / _ \ '_ \ / _` |                 *   
*               | |_) | | / ___ \ |_| | | |  __/ | | | (_| |                 *    
*               |_.__/|_|/_/   \_\__|_| |_|\___|_| |_|\__,_|                 *    
*                                                                            *
*                                                                            *
* \file src/map/homunculus.c                                                 *
* Descri��o Prim�ria.                                                        *
* Descri��o mais elaborada sobre o arquivo.                                  *
* \author brAthena, Athena, eAthena                                          *
* \date ?                                                                    *
* \todo ?                                                                    *  
*****************************************************************************/

#include "../common/cbasetypes.h"
#include "../common/malloc.h"
#include "../common/socket.h"
#include "../common/timer.h"
#include "../common/nullpo.h"
#include "../common/mmo.h"
#include "../common/random.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"
#include "../common/utils.h"

#include "log.h"
#include "clif.h"
#include "chrif.h"
#include "intif.h"
#include "itemdb.h"
#include "map.h"
#include "pc.h"
#include "status.h"
#include "skill.h"
#include "mob.h"
#include "pet.h"
#include "battle.h"
#include "party.h"
#include "guild.h"
#include "atcommand.h"
#include "script.h"
#include "npc.h"
#include "trade.h"
#include "unit.h"

#include "homunculus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct homunculus_interface homunculus_s;

//Returns the viewdata for homunculus
struct view_data* homunculus_get_viewdata(int class_) {
	if (homdb_checkid(class_))
		return &homun->viewdb[class_-HM_CLASS_BASE];
	return NULL;
}

enum homun_type homunculus_class2type(int class_) {
	switch(class_) {
		// Normal Homunculus
		case 6001: case 6005:
		case 6002: case 6006:
		case 6003: case 6007:
		case 6004: case 6008:
			return HT_REG;
		// Evolved Homunculus
		case 6009: case 6013:
		case 6010: case 6014:
		case 6011: case 6015:
		case 6012: case 6016:
			return HT_EVO;
		// Homunculus S
		case 6048:
		case 6049:
		case 6050:
		case 6051:
		case 6052:
			return HT_S;
		default:
			return HT_INVALID;
	}
}

void homunculus_addspiritball(struct homun_data *hd, int max) {
	nullpo_retv(hd);

	if(max > MAX_SKILL_LEVEL)
		max = MAX_SKILL_LEVEL;
	if(hd->homunculus.spiritball < 0)
		hd->homunculus.spiritball = 0;

	if(hd->homunculus.spiritball && hd->homunculus.spiritball >= max) {
		hd->homunculus.spiritball = max;
	} else
		hd->homunculus.spiritball++;

	clif_spiritball(&hd->bl);
}

void homunculus_delspiritball(struct homun_data *hd, int count, int type) {
	nullpo_retv(hd);

	if(hd->homunculus.spiritball <= 0) {
		hd->homunculus.spiritball = 0;
		return;
	}
	if(count <= 0)
		return;
	if(count > MAX_SKILL_LEVEL)
		count = MAX_SKILL_LEVEL;
	if(count > hd->homunculus.spiritball)
		count = hd->homunculus.spiritball;

	hd->homunculus.spiritball -= count;
	if(!type)
		clif_spiritball(&hd->bl);
}

void homunculus_damaged(struct homun_data *hd) {
	clif_hominfo(hd->master,hd,0);
}

int homunculus_dead(struct homun_data *hd) {
	//There's no intimacy penalties on death (from Tharis)
	struct map_session_data *sd = hd->master;

	clif_emotion(&hd->bl, E_WAH);

	//Delete timers when dead.
	homun->hunger_timer_delete(hd);
	hd->homunculus.hp = 0;

	if(!sd)  //unit remove map will invoke unit free
		return 3;

	clif_emotion(&sd->bl, E_SOB);
	//Remove from map (if it has no intimacy, it is auto-removed from memory)
	return 3;
}

//Vaporize a character's homun. If flag, HP needs to be 80% or above.
int homunculus_vaporize(struct map_session_data *sd, enum homun_state flag) {
	struct homun_data *hd;

	nullpo_ret(sd);

	hd = sd->hd;
	if(!hd || hd->homunculus.vaporize != HOM_ST_ACTIVE)
		return 0;

	if(status_isdead(&hd->bl))
		return 0; //Can't vaporize a dead homun.

	if(flag == HOM_ST_REST && get_percentage(hd->battle_status.hp, hd->battle_status.max_hp) < 80)
		return 0;

	hd->regen.state.block = 3; //Block regen while vaporized.
	//Delete timers when vaporized.
	homun->hunger_timer_delete(hd);
	hd->homunculus.vaporize = flag;
	if(battle_config.hom_setting&0x40)
		memset(hd->blockskill, 0, sizeof(hd->blockskill));
	clif_hominfo(sd, sd->hd, 0);
	homun->save(hd);
	return unit_remove_map(&hd->bl, CLR_OUTSIGHT, ALC_MARK);
}

//delete a homunculus, completely "killing it".
//Emote is the emotion the master should use, send negative to disable.
int homunculus_delete(struct homun_data *hd, int emote) {
	struct map_session_data *sd;
	nullpo_ret(hd);
	sd = hd->master;

	if(!sd)
		return unit_free(&hd->bl,CLR_DEAD);

	if(emote >= 0)
		clif_emotion(&sd->bl, emote);

	//This makes it be deleted right away.
	hd->homunculus.intimacy = 0;
	// Send homunculus_dead to client
	hd->homunculus.hp = 0;
	clif_hominfo(sd, hd, 0);
	return unit_remove_map(&hd->bl,CLR_OUTSIGHT, ALC_MARK);
}

int homunculus_calc_skilltree(struct homun_data *hd, int flag_evolve) {
	int i, id = 0;
	int j, f = 1;
	int c = 0;

	nullpo_ret(hd);
	/* load previous homunculus form skills first. */
	if(hd->homunculus.prev_class != 0) {
		c = hd->homunculus.prev_class - HM_CLASS_BASE;

		for(i = 0; i < MAX_SKILL_TREE && (id = homun->skill_tree[c][i].id) > 0; i++) {
			if(hd->homunculus.hskill[ id - HM_SKILLBASE ].id)
				continue; //Skill already known.
			if(!battle_config.skillfree) {
				for(j = 0; j < MAX_PC_SKILL_REQUIRE; j++) {
					if(homun->skill_tree[c][i].need[j].id &&
					   homun->checkskill(hd,homun->skill_tree[c][i].need[j].id) < homun->skill_tree[c][i].need[j].lv) {
						f = 0;
						break;
					}
				}
			}
			if(f)
				hd->homunculus.hskill[id-HM_SKILLBASE].id = id;
		}

		f = 1;
	}

	c = hd->homunculus.class_ - HM_CLASS_BASE;

	for(i = 0; i < MAX_SKILL_TREE && (id =  homun->skill_tree[c][i].id) > 0; i++) {
		if(hd->homunculus.hskill[ id - HM_SKILLBASE ].id)
			continue; //Skill already known.
		j = (flag_evolve) ? 10 : hd->homunculus.intimacy;
		if(j <  homun->skill_tree[c][i].intimacylv)
			continue;
		if(!battle_config.skillfree) {
			for(j = 0; j < MAX_PC_SKILL_REQUIRE; j++) {
				if( homun->skill_tree[c][i].need[j].id &&
				   homun->checkskill(hd, homun->skill_tree[c][i].need[j].id) <  homun->skill_tree[c][i].need[j].lv) {
					f = 0;
					break;
				}
			}
		}
		if(f)
			hd->homunculus.hskill[id-HM_SKILLBASE].id = id;
	}

	if(hd->master)
		clif_homskillinfoblock(hd->master);
	return 0;
}

int homunculus_checkskill(struct homun_data *hd,uint16 skill_id) {
	int i = skill_id - HM_SKILLBASE;
	if(!hd)
		return 0;

	if(hd->homunculus.hskill[i].id == skill_id)
		return (hd->homunculus.hskill[i].lv);

	return 0;
}

int homunculus_skill_tree_get_max(int id, int b_class) {
	int i, skill_id;
	b_class -= HM_CLASS_BASE;
	for(i=0; (skill_id=homun->skill_tree[b_class][i].id)>0; i++)
		if(id == skill_id)
			return homun->skill_tree[b_class][i].max;
	return skill_get_max(id);
}

void homunculus_skillup(struct homun_data *hd,uint16 skill_id) {
	int i = 0 ;
	nullpo_retv(hd);

	if(hd->homunculus.vaporize != HOM_ST_ACTIVE)
		return;

	i = skill_id - HM_SKILLBASE;
	if(hd->homunculus.skillpts > 0 &&
	   hd->homunculus.hskill[i].id &&
	   hd->homunculus.hskill[i].flag == SKILL_FLAG_PERMANENT && //Don't allow raising while you have granted skills. [Skotlex]
	   hd->homunculus.hskill[i].lv < homun->skill_tree_get_max(skill_id, hd->homunculus.class_)
	  ) {
		hd->homunculus.hskill[i].lv++;
		hd->homunculus.skillpts-- ;
		status_calc_homunculus(hd,SCO_NONE);
		if(hd->master) {
			clif_homskillup(hd->master, skill_id);
			clif_hominfo(hd->master,hd,0);
			clif_homskillinfoblock(hd->master);
		}
	}
}

bool homunculus_levelup(struct homun_data *hd) {
	struct s_homunculus *hom;
	struct h_stats *min, *max;
	int growth_str, growth_agi, growth_vit, growth_int, growth_dex, growth_luk ;
	int growth_max_hp, growth_max_sp;
	enum homun_type htype;

	if( (htype = homun->class2type(hd->homunculus.class_)) == HT_INVALID ) {
		ShowError("homunculus_levelup: Invalid class %d. \n", hd->homunculus.class_);
		return false;
	}

	if(!hd->exp_next || hd->homunculus.exp < hd->exp_next)
		return false;
	
	switch( htype ) {
		case HT_REG:
		case HT_EVO:
			if(hd->homunculus.level >= battle_config.hom_max_level)
				return false;
			break;
		case HT_S:
			if(hd->homunculus.level >= battle_config.hom_S_max_level)
				return false;
			break;
	}

	hom = &hd->homunculus;
	hom->level++ ;
	if(!(hom->level % 3))
		hom->skillpts++;   //1 skillpoint each 3 base level

	hom->exp -= hd->exp_next ;
	hd->exp_next = homun->exptable[hom->level - 1] ;

	max  = &hd->homunculusDB->gmax;
	min  = &hd->homunculusDB->gmin;

	growth_max_hp = rnd_value(min->HP, max->HP);
	growth_max_sp = rnd_value(min->SP, max->SP);
	growth_str = rnd_value(min->str, max->str);
	growth_agi = rnd_value(min->agi, max->agi);
	growth_vit = rnd_value(min->vit, max->vit);
	growth_dex = rnd_value(min->dex, max->dex);
	growth_int = rnd_value(min->int_,max->int_);
	growth_luk = rnd_value(min->luk, max->luk);

	//Aegis discards the decimals in the stat growth values!
	growth_str-=growth_str%10;
	growth_agi-=growth_agi%10;
	growth_vit-=growth_vit%10;
	growth_dex-=growth_dex%10;
	growth_int-=growth_int%10;
	growth_luk-=growth_luk%10;

	hom->max_hp += growth_max_hp;
	hom->max_sp += growth_max_sp;
	hom->str += growth_str;
	hom->agi += growth_agi;
	hom->vit += growth_vit;
	hom->dex += growth_dex;
	hom->int_+= growth_int;
	hom->luk += growth_luk;

	if(battle_config.homunculus_show_growth) {
		char output[256] ;
		sprintf(output,
			read_message("Source.map.map_homunculus_s2"),
		        growth_max_hp, growth_max_sp,
		        growth_str/10.0, growth_agi/10.0, growth_vit/10.0,
		        growth_int/10.0, growth_dex/10.0, growth_luk/10.0);
		clif_disp_onlyself(hd->master,output,strlen(output));
	}
	return true ;
}

int homunculus_change_class(struct homun_data *hd, short class_) {
	int i;
	i = homun->db_search(class_,HOMUNCULUS_CLASS);
	if(i < 0)
		return 0;
	hd->homunculusDB = &homun->db[i];
	hd->homunculus.class_ = class_;
	status_set_viewdata(&hd->bl, class_);
	homun->calc_skilltree(hd, 1);
	return 1;
}

bool homunculus_evolve(struct homun_data *hd) {
	struct s_homunculus *hom;
	struct h_stats *max, *min;
	struct map_session_data *sd;
	nullpo_ret(hd);

	sd = hd->master;
	if (!sd)
		return false;

	if(!hd->homunculusDB->evo_class || hd->homunculus.class_ == hd->homunculusDB->evo_class) {
		clif_emotion(&hd->bl, E_SWT);
		return false;
	}

	if (!homun->change_class(hd, hd->homunculusDB->evo_class)) {
		ShowError("homunculus_evolve: Can't evolve homunc from %d to %d", hd->homunculus.class_, hd->homunculusDB->evo_class);
		return false;
	}

	//Apply evolution bonuses
	hom = &hd->homunculus;
	max = &hd->homunculusDB->emax;
	min = &hd->homunculusDB->emin;
	hom->max_hp += rnd_value(min->HP, max->HP);
	hom->max_sp += rnd_value(min->SP, max->SP);
	hom->str += 10*rnd_value(min->str, max->str);
	hom->agi += 10*rnd_value(min->agi, max->agi);
	hom->vit += 10*rnd_value(min->vit, max->vit);
	hom->int_+= 10*rnd_value(min->int_,max->int_);
	hom->dex += 10*rnd_value(min->dex, max->dex);
	hom->luk += 10*rnd_value(min->luk, max->luk);
	hom->intimacy = 500;

	unit_remove_map(&hd->bl, CLR_OUTSIGHT, ALC_MARK);
	map_addblock(&hd->bl);

	clif_spawn(&hd->bl);
	clif_emotion(&sd->bl, E_NO1);
	clif_specialeffect(&hd->bl,568,AREA);

	//status_Calc flag&1 will make current HP/SP be reloaded from hom structure
	hom->hp = hd->battle_status.hp;
	hom->sp = hd->battle_status.sp;
	status_calc_homunculus(hd,SCO_FIRST);

	if(!(battle_config.hom_setting&0x2))
		skill_unit_move(&sd->hd->bl,gettick(),1); // apply land skills immediately

	return true ;
}

bool homunculus_mutate(struct homun_data *hd, int homun_id) {
	struct s_homunculus *hom;
	struct map_session_data *sd;
	int prev_class = 0;
	enum homun_type m_class, m_id;
	nullpo_ret(hd);

	sd = hd->master;
	if (!sd)
		return false;

	m_class = homun->class2type(hd->homunculus.class_);
	m_id    = homun->class2type(homun_id);

	if( m_class == HT_INVALID || m_id == HT_INVALID || m_class != HT_EVO || m_id != HT_S ) {
		clif_emotion(&hd->bl, E_SWT);
		return false;
	}

	prev_class = hd->homunculus.class_;

	if(!homun->change_class(hd, homun_id)) {
		ShowError("homunculus_mutate: Can't evolve homunc from %d to %d", hd->homunculus.class_, homun_id);
		return false;
	}

	unit_remove_map(&hd->bl, CLR_OUTSIGHT, ALC_MARK);
	map_addblock(&hd->bl);

	clif_spawn(&hd->bl);
	clif_emotion(&sd->bl, E_NO1);
	clif_specialeffect(&hd->bl,568,AREA);


	//status_Calc flag&1 will make current HP/SP be reloaded from hom structure
	hom = &hd->homunculus;
	hom->hp = hd->battle_status.hp;
	hom->sp = hd->battle_status.sp;
	hom->prev_class = prev_class;
	status_calc_homunculus(hd,SCO_FIRST);

	if(!(battle_config.hom_setting&0x2))
		skill_unit_move(&sd->hd->bl,gettick(),1); // apply land skills immediately

	return true;
}

int homunculus_gainexp(struct homun_data *hd,unsigned int exp) {
	enum homun_type htype;

	if(hd->homunculus.vaporize != HOM_ST_ACTIVE)
		return 1;

	if((htype = homun->class2type(hd->homunculus.class_)) == HT_INVALID) {
		ShowError("homunculus_gainexp: Invalid class %d. \n", hd->homunculus.class_);
		return 0;
	}

	switch( htype ) {
		case HT_REG:
		case HT_EVO:
			if(hd->homunculus.level >= battle_config.hom_max_level)
				return 0;
			break;
		case HT_S:
			if(hd->homunculus.level >= battle_config.hom_S_max_level)
				return 0;
			break;
	}

	hd->homunculus.exp += exp;

	if(hd->homunculus.exp < hd->exp_next) {
		clif_hominfo(hd->master,hd,0);
		return 0;
	}

 	//levelup
	while(hd->homunculus.exp > hd->exp_next && homun->levelup(hd));

	if(hd->exp_next == 0)
		hd->homunculus.exp = 0 ;

	clif_specialeffect(&hd->bl,568,AREA);
	status_calc_homunculus(hd,SCO_NONE);
	status_percent_heal(&hd->bl, 100, 100);
	return 0;
}

// Return the new value
unsigned int homunculus_add_intimacy(struct homun_data *hd, unsigned int value) {
	if(battle_config.homunculus_friendly_rate != 100)
		value = (value * battle_config.homunculus_friendly_rate) / 100;

	if(hd->homunculus.intimacy + value <= 100000)
		hd->homunculus.intimacy += value;
	else
		hd->homunculus.intimacy = 100000;
	return hd->homunculus.intimacy;
}

// Return 0 if decrease fails or intimacy became 0 else the new value
unsigned int homunculus_consume_intimacy(struct homun_data *hd, unsigned int value) {
	if(hd->homunculus.intimacy >= value)
		hd->homunculus.intimacy -= value;
	else
		hd->homunculus.intimacy = 0;

	return hd->homunculus.intimacy;
}

void homunculus_healed (struct homun_data *hd) {
	clif_hominfo(hd->master,hd,0);
}

void homunculus_save(struct homun_data *hd) {
	// copy data that must be saved in homunculus struct ( hp / sp )
	TBL_PC *sd = hd->master;
	//Do not check for max_hp/max_sp caps as current could be higher to max due
	//to status changes/skills (they will be capped as needed upon stat
	//calculation on login)
	hd->homunculus.hp = hd->battle_status.hp;
	hd->homunculus.sp = hd->battle_status.sp;
	intif_homunculus_requestsave(sd->status.account_id, &hd->homunculus);
}

unsigned char homunculus_menu(struct map_session_data *sd,unsigned char menu_num) {
	nullpo_ret(sd);
	if(sd->hd == NULL)
		return 1;

	switch(menu_num) {
		case 0:
			break;
		case 1:
			homun->feed(sd, sd->hd);
			break;
		case 2:
			homun->delete(sd->hd, -1);
			break;
		default:
			ShowError(read_message("Source.map.map_homunculus_s6"), menu_num) ;
			break;
	}
	return 0;
}

bool homunculus_feed(struct map_session_data *sd, struct homun_data *hd) {
	int i, foodID, emotion;

	if(hd->homunculus.vaporize == HOM_ST_REST)
		return false;

	foodID = hd->homunculusDB->foodID;
	i = pc_search_inventory(sd,foodID);
	if(i < 0) {
		clif_hom_food(sd,foodID,0);
		return false;
	}
	pc_delitem(sd,i,1,0,0,LOG_TYPE_CONSUME);

	if(hd->homunculus.hunger >= 91) {
		homun->consume_intimacy(hd, 50);
		emotion = E_WAH;
	} else if(hd->homunculus.hunger >= 76) {
		homun->consume_intimacy(hd, 5);
		emotion = E_SWT2;
	} else if(hd->homunculus.hunger >= 26) {
		homun->add_intimacy(hd, 75);
		emotion = E_HO;
	} else if(hd->homunculus.hunger >= 11) {
		homun->add_intimacy(hd, 100);
		emotion = E_HO;
	} else {
		homun->add_intimacy(hd, 50);
		emotion = E_HO;
	}

	hd->homunculus.hunger += 10;    //dunno increase value for each food
	if(hd->homunculus.hunger > 100)
		hd->homunculus.hunger = 100;

	clif_emotion(&hd->bl,emotion);
	clif_send_homdata(sd,SP_HUNGRY,hd->homunculus.hunger);
	clif_send_homdata(sd,SP_INTIMATE,hd->homunculus.intimacy / 100);
	clif_hom_food(sd,foodID,1);

	// Too much food :/
	if(hd->homunculus.intimacy == 0)
		return homun->delete(sd->hd, E_OMG);

	return true;
}

int homunculus_hunger_timer(int tid, int64 tick, int id, intptr_t data) {
	struct map_session_data *sd;
	struct homun_data *hd;

	if(!(sd=map_id2sd(id)) || !sd->status.hom_id || !(hd=sd->hd))
		return 1;

	if(hd->hungry_timer != tid) {
		ShowError(read_message("Source.map.map_homunculus_s7"),hd->hungry_timer,tid);
		return 0;
	}

	hd->hungry_timer = INVALID_TIMER;

	hd->homunculus.hunger-- ;
	if(hd->homunculus.hunger <= 10) {
		clif_emotion(&hd->bl, E_AN);
	} else if(hd->homunculus.hunger == 25) {
		clif_emotion(&hd->bl, E_HMM);
	} else if(hd->homunculus.hunger == 75) {
		clif_emotion(&hd->bl, E_OK);
	}

	if(hd->homunculus.hunger < 0) {
		hd->homunculus.hunger = 0;
		// Delete the homunculus if intimacy <= 100
		if(!homun->consume_intimacy(hd, 100))
			return homun->delete(hd, E_OMG);
		clif_send_homdata(sd,SP_INTIMATE,hd->homunculus.intimacy / 100);
	}

	clif_send_homdata(sd,SP_HUNGRY,hd->homunculus.hunger);
	hd->hungry_timer = add_timer(tick+hd->homunculusDB->hungryDelay,homun->hunger_timer,sd->bl.id,0); //simple Fix albator
	return 0;
}

void homunculus_hunger_timer_delete(struct homun_data *hd) {
	nullpo_retv(hd);
	if(hd->hungry_timer != INVALID_TIMER) {
		delete_timer(hd->hungry_timer,homun->hunger_timer);
		hd->hungry_timer = INVALID_TIMER;
	}
}

int homunculus_change_name(struct map_session_data *sd,char *name) {
	int i;
	struct homun_data *hd;
	nullpo_retr(1, sd);

	hd = sd->hd;
	if (!homun_alive(hd))
		return 1;
	if(hd->homunculus.rename_flag && !battle_config.hom_rename)
		return 1;

	for(i=0; i<NAME_LENGTH && name[i]; i++) {
		if(!(name[i]&0xe0) || name[i]==0x7f)
			return 1;
	}

	return intif_rename_hom(sd, name);
}

bool homunculus_change_name_ack(struct map_session_data *sd, char* name, int flag) {
	struct homun_data *hd = sd->hd;
	if (!homun_alive(hd)) return false;

	normalize_name(name," ");//bugreport:3032

	if(!flag || !strlen(name)) {
		clif_displaymessage(sd->fd, msg_txt(280)); // You cannot use this name
		return false;
	}
	safestrncpy(hd->homunculus.name,name,NAME_LENGTH);
	clif_charnameack(0,&hd->bl);
	hd->homunculus.rename_flag = 1;
	clif_hominfo(sd,hd,0);
	return true;
}

int homunculus_db_search(int key,int type) {
	int i;

	for(i=0;i<MAX_HOMUNCULUS_CLASS;i++) {
		if(homun->db[i].base_class <= 0)
			continue;
		switch(type) {
			case HOMUNCULUS_CLASS:
				if(homun->db[i].base_class == key ||
					homun->db[i].evo_class == key)
					return i;
				break;
			case HOMUNCULUS_FOOD:
				if(homun->db[i].foodID == key)
					return i;
				break;
			default:
				return -1;
		}
	}
	return -1;
}

// Create homunc structure
bool homunculus_create(struct map_session_data *sd, struct s_homunculus *hom) {
	struct homun_data *hd;
	int i = 0;

	nullpo_retr(false, sd);

	Assert((sd->status.hom_id == 0 || sd->hd == 0) || sd->hd->master == sd);

	i = homun->db_search(hom->class_,HOMUNCULUS_CLASS);
	if(i < 0) {
		ShowError(read_message("Source.map.map_homunculus_s8"), hom->class_, hom->name);
		sd->status.hom_id = 0;
		intif_homunculus_requestdelete(hom->hom_id);
		return false;
	}
	sd->hd = hd = (struct homun_data *)aCalloc(1,sizeof(struct homun_data));
	hd->bl.type = BL_HOM;
	hd->bl.id = npc_get_new_npc_id();

	hd->master = sd;
	hd->homunculusDB = &homun->db[i];
	memcpy(&hd->homunculus, hom, sizeof(struct s_homunculus));
	hd->exp_next = homun->exptable[hd->homunculus.level - 1];

	status_set_viewdata(&hd->bl, hd->homunculus.class_);
	status_change_init(&hd->bl);
	unit_dataset(&hd->bl);
	hd->ud.dir = sd->ud.dir;

	// Find a random valid pos around the player
	hd->bl.m = sd->bl.m;
	hd->bl.x = sd->bl.x;
	hd->bl.y = sd->bl.y;
	unit_calc_pos(&hd->bl, sd->bl.x, sd->bl.y, sd->ud.dir);
	hd->bl.x = hd->ud.to_x;
	hd->bl.y = hd->ud.to_y;
	hd->masterteleport_timer = 0;

	map_addiddb(&hd->bl);
	status_calc_homunculus(hd,SCO_FIRST);
	status_percent_heal(&hd->bl, 100, 100);

	hd->hungry_timer = INVALID_TIMER;
	return true;
}

void homunculus_init_timers(struct homun_data * hd) {
	if(hd->hungry_timer == INVALID_TIMER)
		hd->hungry_timer = add_timer(gettick()+hd->homunculusDB->hungryDelay,homun->hunger_timer,hd->master->bl.id,0);
	hd->regen.state.block = 0; //Restore HP/SP block.
}

bool homunculus_call(struct map_session_data *sd) {
	struct homun_data *hd;

	if(!sd->status.hom_id) //Create a new homun.
		return homun->creation_request(sd, HM_CLASS_BASE + rnd_value(0, 7));

	// If homunc not yet loaded, load it
	if(!sd->hd)
		return intif_homunculus_requestload(sd->status.account_id, sd->status.hom_id);

	hd = sd->hd;

	if(hd->homunculus.vaporize != HOM_ST_REST)
		return false; //Can't use this if homun wasn't vaporized.

	homun->init_timers(hd);
	hd->homunculus.vaporize = HOM_ST_ACTIVE;
	if(hd->bl.prev == NULL) { //Spawn him
		hd->bl.x = sd->bl.x;
		hd->bl.y = sd->bl.y;
		hd->bl.m = sd->bl.m;
		map_addblock(&hd->bl);
		clif_spawn(&hd->bl);
		clif_send_homdata(sd,SP_ACK,0);
		clif_hominfo(sd,hd,1);
		clif_hominfo(sd,hd,0); // send this x2. dunno why, but kRO does that [blackhole89]
		clif_homskillinfoblock(sd);
		if(battle_config.slaves_inherit_speed&1)
			status_calc_bl(&hd->bl, SCB_SPEED);
		homun->save(hd);
	} else
		//Warp him to master.
		unit_warp(&hd->bl,sd->bl.m, sd->bl.x, sd->bl.y,CLR_OUTSIGHT);
	return true;
}

// Recv homunculus data from char server
bool homunculus_recv_data(int account_id, struct s_homunculus *sh, int flag) {
	struct map_session_data *sd;
	struct homun_data *hd;

	sd = map_id2sd(account_id);
	if(!sd)
		return false;
	if(sd->status.char_id != sh->char_id) {
		if(sd->status.hom_id == sh->hom_id)
			sh->char_id = sd->status.char_id; //Correct char id.
		else
			return false;
	}
	if(!flag) { // Failed to load
		sd->status.hom_id = 0;
		return false;
	}

	if(!sd->status.hom_id)  //Hom just created.
		sd->status.hom_id = sh->hom_id;
	if(sd->hd)  //uh? Overwrite the data.
		memcpy(&sd->hd->homunculus, sh, sizeof(struct s_homunculus));
	else
		homun->create(sd, sh);

	hd = sd->hd;
	if(hd && hd->homunculus.hp && hd->homunculus.vaporize == HOM_ST_ACTIVE && hd->bl.prev == NULL && sd->bl.prev != NULL) {
		enum homun_type htype = homun->class2type(hd->homunculus.class_);

		map_addblock(&hd->bl);
		clif_spawn(&hd->bl);
		clif_send_homdata(sd,SP_ACK,0);
		clif_hominfo(sd,hd,1);
		clif_hominfo(sd,hd,0); // send this x2. dunno why, but kRO does that [blackhole89]
		clif_homskillinfoblock(sd);
		homun->init_timers(hd);
		/* force shuffle if your level is higher than the allowed */
		switch(htype) {
			case HT_REG:
			case HT_EVO:
				if(hd->homunculus.level > battle_config.hom_max_level)
					homun->shuffle(hd);
				break;
			case HT_S:
				if(hd->homunculus.level > battle_config.hom_S_max_level)
					homun->shuffle(hd);
				break;
		}

	}
	return true;
}

// Ask homunculus creation to char server
bool homunculus_creation_request(struct map_session_data *sd, int class_) {
	struct s_homunculus hom;
	struct h_stats *base;
	int i;

	nullpo_retr(false, sd);

	i = homun->db_search(class_,HOMUNCULUS_CLASS);
	if(i < 0) return false;

	memset(&hom, 0, sizeof(struct s_homunculus));
	//Initial data
	safestrncpy(hom.name, homun->db[i].name, NAME_LENGTH-1);
	hom.class_ = class_;
	hom.level = 1;
	hom.hunger = 32; //32%
	hom.intimacy = 2100; //21/1000
	hom.char_id = sd->status.char_id;

	hom.hp = 10 ;
	base = &homun->db[i].base;
	hom.max_hp = base->HP;
	hom.max_sp = base->SP;
	hom.str = base->str *10;
	hom.agi = base->agi *10;
	hom.vit = base->vit *10;
	hom.int_= base->int_*10;
	hom.dex = base->dex *10;
	hom.luk = base->luk *10;

	// Request homunculus creation
	intif_homunculus_create(sd->status.account_id, &hom);
	return true;
}

bool homunculus_ressurect(struct map_session_data* sd, unsigned char per, short x, short y) {
	struct homun_data* hd;
	nullpo_retr(false,sd);

	if(!sd->status.hom_id)
		return false; // no homunculus

	if(!sd->hd)  //Load homun data;
		return intif_homunculus_requestload(sd->status.account_id, sd->status.hom_id);

	hd = sd->hd;

	if(hd->homunculus.vaporize != HOM_ST_ACTIVE)
		return false; // vaporized homunculi need to be 'called'

	if(!status_isdead(&hd->bl))
		return false; // already alive

	homun->init_timers(hd);

	if(!hd->bl.prev) {
		//Add it back to the map.
		hd->bl.m = sd->bl.m;
		hd->bl.x = x;
		hd->bl.y = y;
		map_addblock(&hd->bl);
		clif_spawn(&hd->bl);
	}
	status_revive(&hd->bl, per, 0);
	return true;
}

void homunculus_revive(struct homun_data *hd, unsigned int hp, unsigned int sp) {
	struct map_session_data *sd = hd->master;
	hd->homunculus.hp = hd->battle_status.hp;
	if(!sd)
		return;
	clif_send_homdata(sd,SP_ACK,0);
	clif_hominfo(sd,hd,1);
	clif_hominfo(sd,hd,0);
	clif_homskillinfoblock(sd);
}
//Resets a homunc stats back to zero (but doesn't touches hunger or intimacy)
void homunculus_stat_reset(struct homun_data *hd) {
	struct s_homunculus_db *db;
	struct s_homunculus *hom;
	struct h_stats *base;
	hom = &hd->homunculus;
	db = hd->homunculusDB;
	base = &db->base;
	hom->level = 1;
	hom->hp = 10;
	hom->max_hp = base->HP;
	hom->max_sp = base->SP;
	hom->str = base->str *10;
	hom->agi = base->agi *10;
	hom->vit = base->vit *10;
	hom->int_= base->int_*10;
	hom->dex = base->dex *10;
	hom->luk = base->luk *10;
	hom->exp = 0;
	hd->exp_next = homun->exptable[0];
	memset(&hd->homunculus.hskill, 0, sizeof hd->homunculus.hskill);
	hd->homunculus.skillpts = 0;
}

bool homunculus_shuffle(struct homun_data *hd) {
	struct map_session_data *sd;
	int lv, skillpts;
	unsigned int exp;
	struct s_skill b_skill[MAX_HOMUNSKILL];

	if (!homun_alive(hd))
		return false;

	sd = hd->master;
	lv = hd->homunculus.level;
	exp = hd->homunculus.exp;
	memcpy(&b_skill, &hd->homunculus.hskill, sizeof(b_skill));
	skillpts = hd->homunculus.skillpts;

	//Reset values to level 1.
	homun->stat_reset(hd);

	//Level it back up
	do{
		hd->homunculus.exp += hd->exp_next;
	} while(hd->homunculus.level < lv && homun->levelup(hd));

	if(hd->homunculus.class_ == hd->homunculusDB->evo_class) {
		//Evolved bonuses
		struct s_homunculus *hom = &hd->homunculus;
		struct h_stats *max = &hd->homunculusDB->emax, *min = &hd->homunculusDB->emin;
		hom->max_hp += rnd_value(min->HP, max->HP);
		hom->max_sp += rnd_value(min->SP, max->SP);
		hom->str += 10*rnd_value(min->str, max->str);
		hom->agi += 10*rnd_value(min->agi, max->agi);
		hom->vit += 10*rnd_value(min->vit, max->vit);
		hom->int_+= 10*rnd_value(min->int_,max->int_);
		hom->dex += 10*rnd_value(min->dex, max->dex);
		hom->luk += 10*rnd_value(min->luk, max->luk);
	}

	hd->homunculus.exp = exp;
	memcpy(&hd->homunculus.hskill, &b_skill, sizeof(b_skill));
	hd->homunculus.skillpts = skillpts;
	clif_homskillinfoblock(sd);
	status_calc_homunculus(hd,SCO_NONE);
	status_percent_heal(&hd->bl, 100, 100);
	clif_specialeffect(&hd->bl,568,AREA);

	return true;
}

bool homunculus_read_db_sub(char* str[], int columns, int current) {
	int classid;
	struct s_homunculus_db *db;

	//Base Class,Evo Class
	classid = atoi(str[0]);
	if(classid < HM_CLASS_BASE || classid > HM_CLASS_MAX) {
		ShowError(read_message("Source.map.map_homunculus_s9"), classid);
		return false;
	}
	db = &homun->db[current];
	db->base_class = classid;
	classid = atoi(str[1]);
	if(classid < HM_CLASS_BASE || classid > HM_CLASS_MAX) {
		db->base_class = 0;
		ShowError(read_message("Source.map.map_homunculus_s9"), classid);
		return false;
	}
	db->evo_class = classid;
	//Name, Food, Hungry Delay, Base Size, Evo Size, Race, Element, ASPD
	safestrncpy(db->name,str[2],NAME_LENGTH-1);
	db->foodID = atoi(str[3]);
	db->hungryDelay = atoi(str[4]);
	db->base_size = atoi(str[5]);
	db->evo_size = atoi(str[6]);
	db->race = atoi(str[7]);
	db->element = atoi(str[8]);
	db->baseASPD = atoi(str[9]);
	//base HP, SP, str, agi, vit, int, dex, luk
	db->base.HP = atoi(str[10]);
	db->base.SP = atoi(str[11]);
	db->base.str = atoi(str[12]);
	db->base.agi = atoi(str[13]);
	db->base.vit = atoi(str[14]);
	db->base.int_= atoi(str[15]);
	db->base.dex = atoi(str[16]);
	db->base.luk = atoi(str[17]);
	//Growth Min/Max HP, SP, str, agi, vit, int, dex, luk
	db->gmin.HP = atoi(str[18]);
	db->gmax.HP = atoi(str[19]);
	db->gmin.SP = atoi(str[20]);
	db->gmax.SP = atoi(str[21]);
	db->gmin.str = atoi(str[22]);
	db->gmax.str = atoi(str[23]);
	db->gmin.agi = atoi(str[24]);
	db->gmax.agi = atoi(str[25]);
	db->gmin.vit = atoi(str[26]);
	db->gmax.vit = atoi(str[27]);
	db->gmin.int_= atoi(str[28]);
	db->gmax.int_= atoi(str[29]);
	db->gmin.dex = atoi(str[30]);
	db->gmax.dex = atoi(str[31]);
	db->gmin.luk = atoi(str[32]);
	db->gmax.luk = atoi(str[33]);
	//Evolution Min/Max HP, SP, str, agi, vit, int, dex, luk
	db->emin.HP = atoi(str[34]);
	db->emax.HP = atoi(str[35]);
	db->emin.SP = atoi(str[36]);
	db->emax.SP = atoi(str[37]);
	db->emin.str = atoi(str[38]);
	db->emax.str = atoi(str[39]);
	db->emin.agi = atoi(str[40]);
	db->emax.agi = atoi(str[41]);
	db->emin.vit = atoi(str[42]);
	db->emax.vit = atoi(str[43]);
	db->emin.int_= atoi(str[44]);
	db->emax.int_= atoi(str[45]);
	db->emin.dex = atoi(str[46]);
	db->emax.dex = atoi(str[47]);
	db->emin.luk = atoi(str[48]);
	db->emax.luk = atoi(str[49]);

	//Check that the min/max values really are below the other one.
	if(db->gmin.HP > db->gmax.HP)
		db->gmin.HP = db->gmax.HP;
	if(db->gmin.SP > db->gmax.SP)
		db->gmin.SP = db->gmax.SP;
	if(db->gmin.str > db->gmax.str)
		db->gmin.str = db->gmax.str;
	if(db->gmin.agi > db->gmax.agi)
		db->gmin.agi = db->gmax.agi;
	if(db->gmin.vit > db->gmax.vit)
		db->gmin.vit = db->gmax.vit;
	if(db->gmin.int_> db->gmax.int_)
		db->gmin.int_= db->gmax.int_;
	if(db->gmin.dex > db->gmax.dex)
		db->gmin.dex = db->gmax.dex;
	if(db->gmin.luk > db->gmax.luk)
		db->gmin.luk = db->gmax.luk;

	if(db->emin.HP > db->emax.HP)
		db->emin.HP = db->emax.HP;
	if(db->emin.SP > db->emax.SP)
		db->emin.SP = db->emax.SP;
	if(db->emin.str > db->emax.str)
		db->emin.str = db->emax.str;
	if(db->emin.agi > db->emax.agi)
		db->emin.agi = db->emax.agi;
	if(db->emin.vit > db->emax.vit)
		db->emin.vit = db->emax.vit;
	if(db->emin.int_> db->emax.int_)
		db->emin.int_= db->emax.int_;
	if(db->emin.dex > db->emax.dex)
		db->emin.dex = db->emax.dex;
	if(db->emin.luk > db->emax.luk)
		db->emin.luk = db->emax.luk;

	return true;
}

void homunculus_read_db(void) {
	memset(homun->db,0,sizeof(homun->db));
	sv_readsqldb(get_database_name(15), NULL, 50, MAX_HOMUNCULUS_CLASS, homun->read_db_sub);
	return;
}
// <hom class>,<skill id>,<max level>[,<job level>],<req id1>,<req lv1>,<req id2>,<req lv2>,<req id3>,<req lv3>,<req id4>,<req lv4>,<req id5>,<req lv5>,<intimacy lv req>
bool homunculus_read_skill_db_sub(char* split[], int columns, int current) {
	int k, classid;
	int j;
	int minJobLevelPresent = 0;

	if(columns == 15)
		minJobLevelPresent = 1; // MinJobLvl has been added - FIXME: is this extra field even needed anymore?

	// check for bounds [celest]
	classid = atoi(split[0]) - HM_CLASS_BASE;
	if(classid >= MAX_HOMUNCULUS_CLASS) {
		ShowWarning(read_message("Source.map.map_homunculus_s10"), atoi(split[0]));
		return false;
	}

	k = atoi(split[1]); //This is to avoid adding two lines for the same skill. [Skotlex]
	// Search an empty line or a line with the same skill_id (stored in j)
	ARR_FIND(0, MAX_SKILL_TREE, j, !homun->skill_tree[classid][j].id || homun->skill_tree[classid][j].id == k);
	if(j == MAX_SKILL_TREE) {
		ShowWarning(read_message("Source.map.map_homunculus_s11"), k, classid);
		return false;
	}

	homun->skill_tree[classid][j].id = k;
	homun->skill_tree[classid][j].max = atoi(split[2]);
	if (minJobLevelPresent)
		homun->skill_tree[classid][j].joblv = atoi(split[3]);

	for(k = 0; k < MAX_PC_SKILL_REQUIRE; k++) {
		homun->skill_tree[classid][j].need[k].id = atoi(split[3+k*2+minJobLevelPresent]);
		homun->skill_tree[classid][j].need[k].lv = atoi(split[3+k*2+minJobLevelPresent+1]);
	}

	homun->skill_tree[classid][j].intimacylv = atoi(split[13+minJobLevelPresent]);

	return true;
}

void homunculus_skill_db_read(void) {
	memset(homun->skill_tree,0,sizeof(homun->skill_tree));
	sv_readsqldb(get_database_name(16), NULL, 15, -1, homun->read_skill_db_sub);

	return;
}

void homunculus_exp_db_read(void) {
	int HomunLoop = 0;
	char *row;

	memset(homun->exptable, 0, sizeof(homun->exptable));

	if(SQL_ERROR == Sql_Query(dbmysql_handle, "SELECT * FROM `%s`", get_database_name(52)))
		Sql_ShowDebug(dbmysql_handle);

	while(SQL_SUCCESS == Sql_NextRow(dbmysql_handle) && HomunLoop < MAX_LEVEL) {
		Sql_GetData(dbmysql_handle, 0, &row, NULL);
		homun->exptable[HomunLoop] = atoi(row);

		if(homun->exptable[HomunLoop++] == 9999999)
			break;

		if(homun->exptable[MAX_LEVEL - 1]) { // Last permitted level have to be 0!
			ShowWarning(read_message("Source.map.map_homunculus_s12"), MAX_LEVEL);
			homun->exptable[MAX_LEVEL - 1] = 0;
		}
	}

	ShowSQL(read_message("Source.map.map_homunculus_s13"), CL_WHITE, HomunLoop, CL_RESET, CL_WHITE, get_database_name(52), CL_RESET);
	Sql_FreeResult(dbmysql_handle);
}

void homunculus_reload(void) {
	homun->read_db();
	homun->exp_db_read();
}

void homunculus_skill_reload(void) {
	homun->skill_db_read();
}

void do_init_homunculus(void) {
	int class_;
#if VERSION != -1
	homun->read_db();
	homun->exp_db_read();
	homun->skill_db_read();
#endif
	// Add homunc timer function to timer func list [Toms]
	add_timer_func_list(homun->hunger_timer, "merc_hom_hungry");

	//Stock view data for homuncs
	memset(&homun->viewdb, 0, sizeof(homun->viewdb));
	for (class_ = 0; class_ < ARRAYLENGTH(homun->viewdb); class_++)
		homun->viewdb[class_].class_ = HM_CLASS_BASE+class_;
}

void do_final_homunculus(void) {

}

void homunculus_defaults(void) {
	homun = &homunculus_s;

	homun->init = do_init_homunculus;
	homun->final = do_final_homunculus;
	homun->reload = homunculus_reload;
	homun->reload_skill = homunculus_skill_reload;
	/* */
	homun->get_viewdata = homunculus_get_viewdata;
	homun->class2type = homunculus_class2type;
	homun->damaged = homunculus_damaged;
	homun->dead = homunculus_dead;
	homun->vaporize = homunculus_vaporize;
	homun->delete = homunculus_delete;
	homun->checkskill = homunculus_checkskill;
	homun->calc_skilltree = homunculus_calc_skilltree;
	homun->skill_tree_get_max = homunculus_skill_tree_get_max;
	homun->skillup = homunculus_skillup;
	homun->levelup = homunculus_levelup;
	homun->change_class = homunculus_change_class;
	homun->evolve = homunculus_evolve;
	homun->mutate = homunculus_mutate;
	homun->gainexp = homunculus_gainexp;
	homun->add_intimacy = homunculus_add_intimacy;
	homun->consume_intimacy = homunculus_consume_intimacy;
	homun->healed = homunculus_healed;
	homun->save = homunculus_save;
	homun->menu = homunculus_menu;
	homun->feed = homunculus_feed;
	homun->hunger_timer = homunculus_hunger_timer;
	homun->hunger_timer_delete = homunculus_hunger_timer_delete;
	homun->change_name = homunculus_change_name;
	homun->change_name_ack = homunculus_change_name_ack;
	homun->db_search = homunculus_db_search;
	homun->create = homunculus_create;
	homun->init_timers = homunculus_init_timers;
	homun->call = homunculus_call;
	homun->recv_data = homunculus_recv_data;
	homun->creation_request = homunculus_creation_request;
	homun->ressurect = homunculus_ressurect;
	homun->revive = homunculus_revive;
	homun->stat_reset = homunculus_stat_reset;
	homun->shuffle = homunculus_shuffle;
	homun->read_db_sub = homunculus_read_db_sub;
	homun->read_db = homunculus_read_db;
	homun->read_skill_db_sub = homunculus_read_skill_db_sub;
	homun->skill_db_read = homunculus_skill_db_read;
	homun->exp_db_read = homunculus_exp_db_read;
	homun->addspiritball = homunculus_addspiritball;
	homun->delspiritball = homunculus_delspiritball;
}
