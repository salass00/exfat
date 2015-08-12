#include "exfat.h"
#include <string.h>
#include <errno.h>
#include <debugf.h>
#include "diskio.h"

extern const char *EXEC_NAME;

struct exfat_dev
{
	const char* name;
	struct DiskIO* diskio;
	ULONG sector_size;
	QUAD byte_pos;
	UQUAD total_size;
	BOOL read_only;
	BOOL dirty;
};

struct exfat_dev* exfat_open(const char* spec, enum exfat_mode mode)
{
	struct exfat_dev* dev;
	ULONG disk_present, write_protected, disk_ok, sector_size;
	UQUAD total_sectors;

	dev = malloc(sizeof(struct exfat_dev));
	if (dev == NULL)
	{
		exfat_error("failed to allocate memory for device structure");
		return NULL;
	}

	memset(dev, 0, sizeof(*dev));

	dev->name = spec;
#ifdef __AROS__
	if (strcmp(EXEC_NAME, "exfat-handler") == 0) {
#else
	if (strcmp(EXEC_NAME, "exFATFileSystem") == 0) {
#endif
		dev->diskio = DIO_Setup((CONST_STRPTR)spec, NULL);
	} else {
		dev->diskio = DIO_SetupTags((CONST_STRPTR)spec,
			DIOS_DOSType, ID_EXFAT_DISK,
			DIOS_Inhibit, TRUE,
			TAG_END);
	}
	if (dev->diskio == NULL)
	{
		errno = ENODEV;
		goto cleanup;
	}

	DIO_QueryTags(dev->diskio,
		DIOQ_DiskPresent,    &disk_present,
		DIOQ_WriteProtected, &write_protected,
		DIOQ_DiskValid,      &disk_ok,
		DIOQ_TotalSectors,   &total_sectors,
		DIOQ_BytesPerSector, &sector_size,
		TAG_END);

	if (!disk_present || !disk_ok) {
		errno = ENODEV;
		goto cleanup;
	}

	if (write_protected && mode == EXFAT_MODE_RW) {
		errno = EROFS;
		goto cleanup;
	}

	if (write_protected || mode == EXFAT_MODE_RO) {
		dev->read_only = TRUE;
	}

	dev->sector_size = sector_size;
	dev->total_size = total_sectors * sector_size;

	return dev;

cleanup:
	if (dev != NULL) {
		DIO_Cleanup(dev->diskio);
		free(dev);
	}

	return NULL;
}

int exfat_close(struct exfat_dev* dev)
{
	int res = 0;

	if (dev->dirty)
		res = exfat_fsync(dev);

	DIO_Cleanup(dev->diskio);

	free(dev);

	return res;
}

int exfat_fsync(struct exfat_dev* dev)
{
	if (!dev->read_only) {
		if (DIO_FlushIOCache(dev->diskio) != 0) {
			debugf("Failed to sync device %s\n", dev->name);
			return -1;
		}
		dev->dirty = FALSE;
	}

	return 0;
}

enum exfat_mode exfat_get_mode(const struct exfat_dev* dev)
{
	return dev->read_only ? EXFAT_MODE_RO : EXFAT_MODE_RW;
}

fbx_off_t exfat_get_size(const struct exfat_dev* dev)
{
	return dev->total_size;
}

fbx_off_t exfat_seek(struct exfat_dev* dev, fbx_off_t offset, int whence)
{
	QUAD old_pos = dev->byte_pos;

	switch (whence) {
	case SEEK_SET:
		dev->byte_pos = offset;
		break;
	case SEEK_CUR:
		dev->byte_pos += offset;
		break;
	case SEEK_END:
		dev->byte_pos = dev->total_size + offset;
		break;
	default:
		errno = EINVAL;
		goto error;
	}

	if (dev->byte_pos < 0 || dev->byte_pos > dev->total_size) {
		errno = ESPIPE;
		goto error;
	}

	return dev->byte_pos;

error:
	dev->byte_pos = old_pos;
	return -1;
}

static int amiga_read(struct exfat_dev* dev, QUAD offset, void* buffer, size_t count) {
	if (DIO_ReadBytes(dev->diskio, offset, buffer, count) != 0)
		return ESPIPE;
	else
		return 0;
}

static int amiga_write(struct exfat_dev* dev, QUAD offset, const void* buffer, size_t count) {
	if (DIO_WriteBytes(dev->diskio, offset, buffer, count) != 0)
		return ESPIPE;
	else
		return 0;
}

ssize_t exfat_read(struct exfat_dev* dev, void* buffer, size_t size)
{
	errno = amiga_read(dev, dev->byte_pos, buffer, size);
	if (errno) goto error;

	dev->byte_pos += size;

	return size;

error:
	return -1;
}

ssize_t exfat_write(struct exfat_dev* dev, const void* buffer, size_t size)
{
	if (dev->read_only) {
		errno = EROFS;
		return -1;
	}
	dev->dirty = TRUE;

	errno = amiga_write(dev, dev->byte_pos, buffer, size);
	if (errno) goto error;

	dev->byte_pos += size;

	return size;

error:
	return -1;
}

ssize_t exfat_pread(struct exfat_dev* dev, void* buffer, size_t size,
		fbx_off_t offset)
{
	errno = amiga_read(dev, offset, buffer, size);
	if (errno) goto error;

	return size;

error:
	return -1;
}

ssize_t exfat_pwrite(struct exfat_dev* dev, const void* buffer, size_t size,
		fbx_off_t offset)
{
	if (dev->read_only) {
		errno = EROFS;
		return -1;
	}
	dev->dirty = TRUE;

	errno = amiga_write(dev, offset, buffer, size);
	if (errno) goto error;

	return size;

error:
	return -1;
}

ssize_t exfat_generic_pread(const struct exfat* ef, struct exfat_node* node,
		void* buffer, size_t size, fbx_off_t offset)
{
	cluster_t cluster;
	char* bufp = buffer;
	fbx_off_t lsize, loffset, remainder;

	if (offset >= node->size)
		return 0;
	if (size == 0)
		return 0;

	cluster = exfat_advance_cluster(ef, node, offset / CLUSTER_SIZE(*ef->sb));
	if (CLUSTER_INVALID(cluster))
	{
		exfat_error("invalid cluster 0x%x while reading", cluster);
		return -1;
	}

	loffset = offset % CLUSTER_SIZE(*ef->sb);
	remainder = MIN(size, node->size - offset);
	while (remainder > 0)
	{
		if (CLUSTER_INVALID(cluster))
		{
			exfat_error("invalid cluster 0x%x while reading", cluster);
			return -1;
		}
		lsize = MIN(CLUSTER_SIZE(*ef->sb) - loffset, remainder);
		if (exfat_pread(ef->dev, bufp, lsize,
					exfat_c2o(ef, cluster) + loffset) < 0)
		{
			exfat_error("failed to read cluster %#x", cluster);
			return -1;
		}
		bufp += lsize;
		loffset = 0;
		remainder -= lsize;
		cluster = exfat_next_cluster(ef, node, cluster);
	}
	if (!ef->ro && !ef->noatime)
		exfat_update_atime(node);
	return MIN(size, node->size - offset) - remainder;
}

ssize_t exfat_generic_pwrite(struct exfat* ef, struct exfat_node* node,
		const void* buffer, size_t size, fbx_off_t offset)
{
	cluster_t cluster;
	const char* bufp = buffer;
	fbx_off_t lsize, loffset, remainder;

 	if (offset > node->size)
 		if (exfat_truncate(ef, node, offset, true) != 0)
 			return -1;
  	if (offset + size > node->size)
 		if (exfat_truncate(ef, node, offset + size, false) != 0)
 			return -1;
	if (size == 0)
		return 0;

	cluster = exfat_advance_cluster(ef, node, offset / CLUSTER_SIZE(*ef->sb));
	if (CLUSTER_INVALID(cluster))
	{
		exfat_error("invalid cluster 0x%x while writing", cluster);
		return -1;
	}

	loffset = offset % CLUSTER_SIZE(*ef->sb);
	remainder = size;
	while (remainder > 0)
	{
		if (CLUSTER_INVALID(cluster))
		{
			exfat_error("invalid cluster 0x%x while writing", cluster);
			return -1;
		}
		lsize = MIN(CLUSTER_SIZE(*ef->sb) - loffset, remainder);
		if (exfat_pwrite(ef->dev, bufp, lsize,
				exfat_c2o(ef, cluster) + loffset) < 0)
		{
			exfat_error("failed to write cluster %#x", cluster);
			return -1;
		}
		bufp += lsize;
		loffset = 0;
		remainder -= lsize;
		cluster = exfat_next_cluster(ef, node, cluster);
	}
	exfat_update_mtime(node);
	return size - remainder;
}

