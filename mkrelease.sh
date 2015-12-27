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

cp -p exfatfs.readme ${DESTDIR}/exfatfs-${NUMVERS}
cp -p COPYING ${DESTDIR}/exfatfs-${NUMVERS}
cp -p releasenotes ${DESTDIR}/exfatfs-${NUMVERS}
cp -p exfat-handler ${DESTDIR}/exfatfs-${NUMVERS}/L

sed -i "s_^Version:      xx.xx_Version:      ${NUMVERS}_" ${DESTDIR}/exfatfs-${NUMVERS}/exfatfs.readme
sed -i "s_^Requires:     xxx_Requires:     util/libs/filesysbox.${HOST}.lha_" ${DESTDIR}/exfatfs-${NUMVERS}/exfatfs.readme
sed -i "s_^Architecture: xxx_Architecture: ${HOST}_" ${DESTDIR}/exfatfs-${NUMVERS}/exfatfs.readme

rm -f exfatfs.${HOST}.7z
7za u exfatfs.${HOST}.7z ./${DESTDIR}/exfatfs-${NUMVERS}

rm -rf ${DESTDIR}

echo "exfatfs.${HOST}.7z created"

