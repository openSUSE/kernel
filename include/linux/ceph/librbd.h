#ifndef _LIBRBD_H
#define _LIBRBD_H

#include <linux/blk-mq.h>
#include <linux/device.h>

/*
 * The basic unit of block I/O is a sector.  It is interpreted in a
 * number of contexts in Linux (blk, bio, genhd), but the default is
 * universally 512 bytes.  These symbols are just slightly more
 * meaningful than the bare numbers they represent.
 */
#define	SECTOR_SHIFT	9
#define	SECTOR_SIZE	(1ULL << SECTOR_SHIFT)

#define RBD_DRV_NAME "rbd"

/*
 * An RBD device name will be "rbd#", where the "rbd" comes from
 * RBD_DRV_NAME above, and # is a unique integer identifier.
 */
#define DEV_NAME_LEN		32

/*
 * block device image metadata (in-memory version)
 */
struct rbd_image_header {
	/* These six fields never change for a given rbd image */
	char *object_prefix;
	__u8 obj_order;
	u64 stripe_unit;
	u64 stripe_count;
	s64 data_pool_id;
	u64 features;		/* Might be changeable someday? */

	/* The remaining fields need to be updated occasionally */
	u64 image_size;
	struct ceph_snap_context *snapc;
	char *snap_names;	/* format 1 only */
	u64 *snap_sizes;	/* format 1 only */
};

enum obj_request_type {
	OBJ_REQUEST_NODATA, OBJ_REQUEST_BIO, OBJ_REQUEST_PAGES, OBJ_REQUEST_SG,
};

enum obj_operation_type {
	OBJ_OP_WRITE,
	OBJ_OP_READ,
	OBJ_OP_DISCARD,
	OBJ_OP_CMP_AND_WRITE,
	OBJ_OP_WRITESAME,
};

struct rbd_img_request;
typedef void (*rbd_img_callback_t)(struct rbd_img_request *);

struct rbd_obj_request;

struct rbd_img_request {
	struct rbd_device	*rbd_dev;
	u64			offset;	/* starting image byte offset */
	u64			length;	/* byte count from offset */
	unsigned long		flags;

	u64			snap_id;	/* for reads */
	struct ceph_snap_context *snapc;	/* for writes */

	struct request		*rq;		/* block request */
	struct rbd_obj_request	*obj_request;	/* obj req initiator */
	void			*lio_cmd_data;	/* lio specific data */

	struct page		**copyup_pages;
	u32			copyup_page_count;
	spinlock_t		completion_lock;/* protects next_completion */
	u32			next_completion;
	rbd_img_callback_t	callback;
        /*
	 * xferred is the bytes that have successfully been transferred.
	 * completed is the bytes that have been accounted for and includes
	 * failures.
	 */
	u64			xferred;/* aggregate bytes transferred */
	u64			completed;/* aggregate bytes completed */
	int			result;	/* first nonzero obj_request result */

	u32			obj_request_count;
	struct list_head	obj_requests;	/* rbd_obj_request structs */

	struct kref		kref;
};

struct rbd_mapping {
	u64                     size;
	u64                     features;
	bool			read_only;
};

enum rbd_watch_state {
	RBD_WATCH_STATE_UNREGISTERED,
	RBD_WATCH_STATE_REGISTERED,
	RBD_WATCH_STATE_ERROR,
};

enum rbd_lock_state {
	RBD_LOCK_STATE_UNLOCKED,
	RBD_LOCK_STATE_LOCKED,
	RBD_LOCK_STATE_RELEASING,
};

/* WatchNotify::ClientId */
struct rbd_client_id {
	u64 gid;
	u64 handle;
};

struct rbd_client;
struct rbd_spec;
struct rbd_options;

/*
 * a single device
 */
struct rbd_device {
	int			dev_id;		/* blkdev unique id */

	int			major;		/* blkdev assigned major */
	int			minor;
	struct gendisk		*disk;		/* blkdev's gendisk and rq */

	u32			image_format;	/* Either 1 or 2 */
	struct rbd_client	*rbd_client;

	char			name[DEV_NAME_LEN]; /* blkdev name, e.g. rbd3 */

	spinlock_t		lock;		/* queue, flags, open_count */

	struct rbd_image_header	header;
	unsigned long		flags;		/* possibly lock protected */
	struct rbd_spec		*spec;
	struct rbd_options	*opts;
	char			*config_info;	/* add{,_single_major} string */

	struct ceph_object_id	header_oid;
	struct ceph_object_locator header_oloc;

	struct ceph_file_layout	layout;		/* used for all rbd requests */

	struct mutex		watch_mutex;
	enum rbd_watch_state	watch_state;
	struct ceph_osd_linger_request *watch_handle;
	u64			watch_cookie;
	struct delayed_work	watch_dwork;

	struct rw_semaphore	lock_rwsem;
	enum rbd_lock_state	lock_state;
	char			lock_cookie[32];
	struct rbd_client_id	owner_cid;
	struct work_struct	acquired_lock_work;
	struct work_struct	released_lock_work;
	struct delayed_work	lock_dwork;
	struct work_struct	unlock_work;
	wait_queue_head_t	lock_waitq;

	struct workqueue_struct	*task_wq;

	struct rbd_spec		*parent_spec;
	u64			parent_overlap;
	atomic_t		parent_ref;
	struct rbd_device	*parent;

	/* Block layer tags. */
	struct blk_mq_tag_set	tag_set;

	/* protects updating the header */
	struct rw_semaphore     header_rwsem;

	struct rbd_mapping	mapping;

	struct list_head	node;

	/* sysfs related */
	struct device		dev;
	unsigned long		open_count;	/* protected by lock */
};

extern struct rbd_img_request *rbd_img_request_create(
					struct rbd_device *rbd_dev,
					u64 offset, u64 length,
					enum obj_operation_type op_type,
					struct ceph_snap_context *snapc);
extern int rbd_img_cmp_and_write_request_fill(
					struct rbd_img_request *img_request,
					struct scatterlist *cmp_sgl,
					u64 cmp_length,
					struct scatterlist *write_sgl,
					u64 write_length);
extern int rbd_img_request_fill(struct rbd_img_request *img_request,
				enum obj_request_type type, void *data_desc);
extern int rbd_img_request_submit(struct rbd_img_request *img_request);
extern void rbd_img_request_put(struct rbd_img_request *img_request);
extern int rbd_dev_setxattr(struct rbd_device *rbd_dev, char *key, void *val,
			    int val_len);
extern int rbd_dev_cmpsetxattr(struct rbd_device *rbd_dev, char *key,
			       void *oldval, int oldval_len, void *newval,
			       int newval_len);
extern int rbd_dev_getxattr(struct rbd_device *rbd_dev, char *key, int max_val_len,
			    void **_val, int *val_len);

#endif
