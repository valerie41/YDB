/****************************************************************
 *								*
 *	Copyright 2005 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include "gtm_time.h"
#include "gtm_string.h"

#include "gdsroot.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsbt.h"
#include "gdsfhead.h"
#include "filestruct.h"
#include "gdscc.h"
#include "gdskill.h"
#include "jnl.h"
#include "iosp.h"		/* for SS_NORMAL */
#include "util.h"

/* Prototypes */
#include "gtmmsg.h"		/* for gtm_putmsg prototype */
#include "desired_db_format_set.h"
#include "send_msg.h"		/* for send_msg */

LITREF	char			*gtm_dbversion_table[];

GBLREF	inctn_opcode_t		inctn_opcode;
GBLREF	jnl_gbls_t		jgbl;
GBLREF	uint4			process_id;

/* input parameter "command_name" is a string that is either "MUPIP REORG UPGRADE/DOWNGRADE" or "MUPIP SET VERSION" */
int4	desired_db_format_set(gd_region *reg, enum db_ver new_db_format, char *command_name)
{
	boolean_t		was_crit;
	char			*db_fmt_str;
	int4			status;
	uint4			jnl_status;
	inctn_opcode_t		save_inctn_opcode;
	sgmnt_addrs		*csa;
	sgmnt_data_ptr_t	csd;
	trans_num		curr_tn;

	error_def(ERR_MMNODYNDWNGRD);
	error_def(ERR_DBDSRDFMTCHNG);
	error_def(ERR_MUDWNGRDTN);
	error_def(ERR_MUNOACTION);

	assert(reg->open);
	csa = &FILE_INFO(reg)->s_addrs;
	csd = csa->hdr;
	was_crit = csa->now_crit;
	if (FALSE == was_crit)
		grab_crit(reg);
	/* if MM and desired_db_format is not V5, gvcst_init would have issued MMNODYNDWNGRD error. assert that. */
	assert(dba_bg == csd->acc_meth || (dba_mm == csd->acc_meth) && (GDSV5 == csd->desired_db_format));
	if (csd->desired_db_format == new_db_format)
	{	/* no change in db_format. fix max_tn_warn if necessary and return right away. */
		status = ERR_MUNOACTION;
		assert(csd->trans_hist.curr_tn <= csd->max_tn);
		if ((GDSV4 == new_db_format) && (MAX_TN_V4 < csd->max_tn))
		{	/* reset max_tn to MAX_TN_V4 only if V4 format and the new value will still be greater than curr_tn */
			if (MAX_TN_V4 >= csd->trans_hist.curr_tn)
			{
				csd->max_tn = MAX_TN_V4;
				/* since max_tn changed above, max_tn_warn might also need to correspondingly change */
				SET_TN_WARN(csd, csd->max_tn_warn);
			} else
				GTMASSERT;	/* out-of-design state where curr_tn > MAX_TN_V4 in GDSV4 */
		}
		if (FALSE == was_crit)
			rel_crit(reg);
		return status;
	}
	if (dba_mm == csd->acc_meth)
	{
		status = ERR_MMNODYNDWNGRD;
		gtm_putmsg(VARLSTCNT(4) status, 2, REG_LEN_STR(reg));
		if (FALSE == was_crit)
			rel_crit(reg);
		return status;
	}
	/* check if curr_tn is too high to downgrade */
	curr_tn = csd->trans_hist.curr_tn;
	if ((GDSV4 == new_db_format) && (MAX_TN_V4 <= curr_tn))
	{
		status = ERR_MUDWNGRDTN;
		gtm_putmsg(VARLSTCNT(5) status, 3, &curr_tn, DB_LEN_STR(reg));
		if (FALSE == was_crit)
			rel_crit(reg);
		return status;
	}
	csd->desired_db_format = new_db_format;
	csd->fully_upgraded = FALSE;
	csd->desired_db_format_tn = curr_tn;
	switch(new_db_format)
	{
		case GDSV4:
			csd->max_tn = MAX_TN_V4;
			break;
		case GDSV5:
			csd->max_tn = MAX_TN_V5;
			break;
		default:
			GTMASSERT;
	}
	SET_TN_WARN(csd, csd->max_tn_warn);	/* if max_tn changed above, max_tn_warn also needs a corresponding change */
	assert(curr_tn < csd->max_tn);	/* ensure CHECK_TN macro below will not issue TNTOOLARGE rts_error */
	CHECK_TN(csa, csd, curr_tn);	/* can issue rts_error TNTOOLARGE */
	/* write INCTN record and increment csd->trans_hist.curr_tn */
	assert(csd->trans_hist.early_tn == csd->trans_hist.curr_tn);
	if (JNL_ENABLED(csd))
	{
		jnl_status = jnl_ensure_open();
		if (0 != jnl_status)
		{
			gtm_putmsg(VARLSTCNT(6) jnl_status, 4, JNL_LEN_STR(csd), DB_LEN_STR(reg));
			if (FALSE == was_crit)
				rel_crit(reg);
			return jnl_status;
		}
		save_inctn_opcode = inctn_opcode;
		inctn_opcode = inctn_db_format_change;
		JNL_SHORT_TIME(jgbl.gbl_jrec_time);	/* needed for jnl_put_jrt_pini() and jnl_write_inctn_rec() */
		if (0 == csa->jnl->pini_addr)
			jnl_put_jrt_pini(csa);
		jnl_write_inctn_rec(csa);
		inctn_opcode = save_inctn_opcode;
	}
	csd->trans_hist.early_tn = csd->trans_hist.curr_tn + 1;
	INCREMENT_CURR_TN(csd);
	if (FALSE == was_crit)
		rel_crit(reg);
	status = SS_NORMAL;
	send_msg(VARLSTCNT(11) ERR_DBDSRDFMTCHNG, 9, DB_LEN_STR(reg), LEN_AND_STR(gtm_dbversion_table[new_db_format]),
		LEN_AND_STR(command_name), process_id, process_id, &curr_tn);
	return status;
}