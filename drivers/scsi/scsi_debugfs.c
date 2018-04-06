#include <linux/seq_file.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_dbg.h>
#include "scsi_debugfs.h"

void scsi_show_rq(struct seq_file *m, struct request *rq)
{
	struct scsi_cmnd *cmd = container_of(scsi_req(rq), typeof(*cmd), req);
	const u8 *const cdb = READ_ONCE(cmd->cmnd);
	char buf[80] = "(?)";

	if (cdb)
		__scsi_format_command(buf, sizeof(buf), cdb, cmd->cmd_len);
	seq_printf(m, ", .cmd=%s", buf);
}
