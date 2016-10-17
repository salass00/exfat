#!/bin/sh
#
# Script for generating a release archive.
#

HOST="${1:-i386-aros}"

make HOST=${HOST} clean

make HOST=${HOST}

DESTDIR='tmp'
FULLVERS=`version exfat-handler`
NUMVERS=`echo "${FULLVERS}" | cut -d' ' -f2`

rm -rf ${DESTDIR}
mkdir -p ${DESTDIR}/exfatfs-${NUMVERS}/L

cp -p COPYING ${DESTDIR}/exfatfs-${NUMVERS}
cp -p releasenotes ${DESTDIR}/exfatfs-${NUMVERS}
cp -p exfat-handler ${DESTDIR}/exfatfs-${NUMVERS}/L

echo "Short:        A free exFAT file system implementation" > ${DESTDIR}/exfatfs-${NUMVERS}/exfatfs.readme
echo "Author:       Andrew Nayenko, Fredrik Wikstrom" >> ${DESTDIR}/exfatfs-${NUMVERS}/exfatfs.readme
echo "Uploader:     Fredrik Wikstrom <fredrik@a500.org>" >> ${DESTDIR}/exfatfs-${NUMVERS}/exfatfs.readme
echo "Type:         disk/misc" >> ${DESTDIR}/exfatfs-${NUMVERS}/exfatfs.readme
echo "Version:      ${NUMVERS}" >> ${DESTDIR}/exfatfs-${NUMVERS}/exfatfs.readme
echo "Requires:     util/libs/filesysbox.${HOST}.lha" >> ${DESTDIR}/exfatfs-${NUMVERS}/exfatfs.readme
echo "Architecture: ${HOST}" >> ${DESTDIR}/exfatfs-${NUMVERS}/exfatfs.readme
echo "" >> ${DESTDIR}/exfatfs-${NUMVERS}/exfatfs.readme
cat README >> ${DESTDIR}/exfatfs-${NUMVERS}/exfatfs.readme

rm -f exfatfs.${HOST}.7z
7za u exfatfs.${HOST}.7z ./${DESTDIR}/exfatfs-${NUMVERS}

rm -rf ${DESTDIR}

echo "exfatfs.${HOST}.7z created"

