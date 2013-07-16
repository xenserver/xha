#!/bin/sh
if [ -x /usr/bin/hg ] ; then
	ID=`/usr/bin/hg id | awk '{print $1}'`
else
	ID=''
fi
DATE=`date "+%b %e %T %Z %Y"`
echo "#define BUILD_DATE \"$DATE\""
echo "#define BUILD_ID \"$ID\""
