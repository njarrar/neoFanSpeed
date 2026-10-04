#!/usr/bin/env python3
"""Mount a FAT32 image with a user-space FAT driver (pyfatfs) through FUSE.

The Linux kernel FAT driver is not always available in containers, and the
fusefat package cannot append to files, so this small bridge lets Wine write
the log onto a real FAT32 volume. Check the volume afterwards with fsck.vfat.

usage: fat32_fuse.py image.img mountpoint     (stays in the foreground)
needs: pip install pyfatfs; apt install python3-fusepy
"""
import errno
import stat
import sys
import threading

import fusepy as fuse
from fs.errors import DirectoryExpected, FileExpected, ResourceNotFound
from pyfatfs.PyFatFS import PyFatFS


class FatOps(fuse.Operations):
    def __init__(self, image):
        self.fs = PyFatFS(image)
        self.mutex = threading.Lock()
        self.files = {}
        self.next = 1

    def _info(self, path):
        try:
            return self.fs.getinfo(path, namespaces=["details"])
        except ResourceNotFound:
            raise fuse.FuseOSError(errno.ENOENT)

    def getattr(self, path, fh=None):
        with self.mutex:
            if path == "/":
                return dict(st_mode=stat.S_IFDIR | 0o755, st_nlink=2)
            i = self._info(path)
            t = (i.modified.timestamp() if i.modified else 0)
            mode = (stat.S_IFDIR | 0o755) if i.is_dir else (stat.S_IFREG | 0o644)
            return dict(st_mode=mode, st_nlink=1, st_size=i.size or 0, st_mtime=t, st_atime=t, st_ctime=t)

    def readdir(self, path, fh):
        with self.mutex:
            return [".", ".."] + self.fs.listdir(path)

    def mkdir(self, path, mode):
        with self.mutex:
            self.fs.makedir(path)

    def rmdir(self, path):
        with self.mutex:
            self.fs.removedir(path)

    def unlink(self, path):
        with self.mutex:
            self.fs.remove(path)

    def rename(self, old, new):
        with self.mutex:
            self.fs.move(old, new, overwrite=True)

    def _open(self, path, mode):
        f = self.fs.openbin(path, mode)
        fh = self.next
        self.next += 1
        self.files[fh] = f
        return fh

    def create(self, path, mode, fi=None):
        with self.mutex:
            return self._open(path, "w+b")

    def open(self, path, flags):
        with self.mutex:
            try:
                return self._open(path, "r+b")
            except (FileExpected, DirectoryExpected):
                raise fuse.FuseOSError(errno.EISDIR)

    def read(self, path, size, offset, fh):
        with self.mutex:
            f = self.files[fh]
            f.seek(offset)
            return f.read(size)

    def write(self, path, data, offset, fh):
        with self.mutex:
            f = self.files[fh]
            f.seek(offset)
            f.write(data)
            return len(data)

    def truncate(self, path, length, fh=None):
        with self.mutex:
            if fh in self.files:
                self.files[fh].truncate(length)
            else:
                with self.fs.openbin(path, "r+b") as f:
                    f.truncate(length)

    def flush(self, path, fh):
        with self.mutex:
            if fh in self.files:
                self.files[fh].flush()

    def release(self, path, fh):
        with self.mutex:
            f = self.files.pop(fh, None)
            if f:
                f.close()

    def fsync(self, path, datasync, fh):
        return self.flush(path, fh)

    def utimens(self, path, times=None):
        return 0

    def chmod(self, path, mode):
        return 0

    def chown(self, path, uid, gid):
        return 0

    def statfs(self, path):
        return dict(f_bsize=512, f_frsize=512, f_blocks=131072, f_bfree=100000, f_bavail=100000, f_namemax=255)

    def destroy(self, path):
        self.fs.close()


if __name__ == "__main__":
    fuse.FUSE(FatOps(sys.argv[1]), sys.argv[2], foreground=True, nothreads=True)
