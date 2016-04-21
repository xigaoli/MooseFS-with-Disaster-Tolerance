#!/bin/bash
#-----define how to generate dir tree----
FOLDERDEEP=3
MAX_FOLDER_NUM_PER_LAYER=5
MAX_FILE_NUM_PER_LAYER=5

#-----------define file size-------------
FILESIZE_MAX=102400
FILESIZE_MIN=0

#-----------define file path-------------
BASE_PATH=/root/data


#----------------------------------------
#!!!!!!!!!!!!do not modify!!!!!!!!!!!!
FPATH=
GPATH=

FILECOUNT=0
FOLDERCOUNT=0

create_dir()
{
	local dirname=$1
	local depth=$2
	local tmpdir=$1
	local i=0
	echo "${dirname}"
	if [ $depth -lt $FOLDERDEEP ] ; then
		echo "deep is ${depth}"
		local pathmax=$(($RANDOM%$MAX_FOLDER_NUM_PER_LAYER))
		while [ $i -lt $pathmax ]
		#----------create [pathMax] of dir strings in this depth, recursively------
		do
			tmpdir="${dirname}/dep_${depth}_num_${i}_${FOLDERCOUNT}"
			if [ ! -d "${tmpdir}" ] ; then
			FOLDERCOUNT=`expr $FOLDERCOUNT + 1`
			fi

			depth=`expr $depth + 1`
			echo "${depth}"
			#---------recursion create string----------
			create_dir "${tmpdir}" "${depth}"
			depth=`expr $depth - 1`
			i=`expr $i + 1`
		done	
	fi
	
	#---------create string complete-----------
	#------- actually create multiple folders
	if [ ! -d "${dirname}" ] ; then
	echo "Creating Dir ${dirname}"
	mkdir -p ${dirname}
	
	fi
	
	local j=0
	local fileMax=$(($RANDOM%$MAX_FILE_NUM_PER_LAYER+1))
	while [ $j -lt $fileMax ]
	do
		GPATH="${dirname}/dep_${depth}_num_${j}_${FILECOUNT}.test"
		echo "File list: ${GPATH}"
		FILECOUNT=`expr $FILECOUNT + 1`
		./writeRandomByte ${GPATH} ${FILESIZE_MIN} ${FILESIZE_MAX}
		GPATH=""
		j=`expr $j + 1`
	done
	
	
}
create_dir "${BASE_PATH}" 0
echo "Total File Number: ${FILECOUNT}"
echo "Total Folder Number: ${FOLDERCOUNT}"
