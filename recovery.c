#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <openssl/md5.h>

#define bool int
#define true 1
#define false 0

#pragma pack(push, 1)
struct BootEntry{
	unsigned char BS_jmpBoot[3];
	unsigned char BS_OEMName[8];
	unsigned short BPB_BytsPerSec;

	unsigned char BPB_SecPerClus;

	unsigned short BPB_RsvdSecCnt;
	unsigned char BPB_NumFATs;
	unsigned short BPB_RootEntCnt;

	unsigned short BPB_TotSec16;
	unsigned char BPB_Media;
	unsigned short BPB_FATSz16;

	unsigned short BPB_SecPerTrk;
	unsigned short BPB_NumHeads;
	unsigned long BPB_HiddSec;
	unsigned long BPB_TotSec32;

	unsigned long BPB_FATSz32;
	unsigned short BPB_ExtFlags;
	unsigned short BPB_FSver;
	unsigned long BPB_RootClus;

	unsigned short BPB_FSInfo;

	unsigned short BPB_BkBootSec;

	unsigned char BPB_Reserved[12];
	unsigned char BS_DrvNum;
	unsigned char BS_Reverved1;
	unsigned char BS_BootSig;

	unsigned long BS_VolID;
	unsigned char BS_VolLab[11];

	unsigned char BS_FilSystype[8];
};
#pragma pack(pop)

#pragma pack(push, 1)
struct DirEntry{
	unsigned char DIR_Name[11];
	unsigned char DIR_Attr;
	unsigned char DIR_NTRes;
	unsigned char DIR_CrtTimeTenth;
	unsigned short DIR_CrtTime;
	unsigned short DIR_CrtDate;
	unsigned short DIR_LstAccDate;
	unsigned short DIR_FstClusHI;
	unsigned short DIR_WrtTime;
	unsigned short DIR_WrtDate;
	unsigned short DIR_FstClusLO;
	unsigned long DIR_FileSize;
};
#pragma pack(pop)

struct DirEntryList{
	struct DirEntryList *prev;
	struct DirEntry dirEntry;
	struct DirEntryList *next;
};

char *programName;
char *deviceFileName;
char *fileName;
char *targetMD5;
int *FAT;
struct BootEntry *bootEntry;
struct DirEntryList *dirEntryList;

void displayUsageThenExit(){
	printf("Usage: %s -d [device filename] [other arguments]\n", programName);
	printf("-i Print boot sector information.\n");
	printf("-l List the root directory.\n");
	printf("-r filename [-m md5] File recovery.\n");
	exit(EXIT_FAILURE);
}

void fileNotFoundThenExit(){
	printf("%s: file not found.\n", fileName);
	exit(EXIT_FAILURE);
}

void ambiguousThenExit(){
	printf("%s: error - ambiguous.\n", fileName);
	exit(EXIT_FAILURE);
}

void readBootSectorInformation(struct BootEntry *bootEntry){
	int fileDescriptor;
	ssize_t bytesRead;

	fileDescriptor = open(deviceFileName, O_RDONLY);
	if(fileDescriptor < 0)
		displayUsageThenExit();
	bytesRead = read(fileDescriptor, bootEntry, 90); /*sizeof(struct BootEntry) == 90*/
	if(bytesRead < 0)
		displayUsageThenExit();

	close(fileDescriptor);
}

void printBootSectorInformation(){
	printf("Number of FATs = %d\n", bootEntry->BPB_NumFATs);
	printf("Number of bytes per sector = %d\n", bootEntry->BPB_BytsPerSec);
	printf("Number of sectors per cluster = %d\n", bootEntry->BPB_SecPerClus);
	printf("Number of reserved sectors = %d\n", bootEntry->BPB_RsvdSecCnt);
}

void readRootDirectory(){
	int fileDescriptor;
	ssize_t bytesRead;
	struct DirEntry *dirEntry;
	struct DirEntryList *tempDirEntryList;
	int nextCluster, i, FATValue = bootEntry->BPB_RootClus;

	/*Buffer for containing reserved area*/
	char* buffer = (char*)malloc(bootEntry->BPB_RsvdSecCnt*bootEntry->BPB_BytsPerSec);

	//Open device
	fileDescriptor = open(deviceFileName, O_RDONLY);
	if(fileDescriptor < 0)
		displayUsageThenExit();
	
	//Read boot sector
	bytesRead = read(fileDescriptor, buffer, bootEntry->BPB_RsvdSecCnt*bootEntry->BPB_BytsPerSec);
	if(bytesRead < 0)
		displayUsageThenExit();

	//Read FAT
	bytesRead += read(fileDescriptor, FAT, bootEntry->BPB_NumFATs*bootEntry->BPB_FATSz32*bootEntry->BPB_BytsPerSec);

	//Read cluster between FAT and root directory(0 byte if it start form cluster 2)
	bytesRead += read(fileDescriptor, buffer, (bootEntry->BPB_RootClus-2)*bootEntry->BPB_BytsPerSec);
	
	tempDirEntryList = dirEntryList;
	dirEntry = &(tempDirEntryList->dirEntry);

	while(FATValue < 0x0ffffff8 ){
		bytesRead = 0;
		while(bytesRead < bootEntry->BPB_SecPerClus*bootEntry->BPB_BytsPerSec){
			//Read in a directory entry
			bytesRead += read(fileDescriptor, dirEntry, sizeof(struct DirEntry));

			tempDirEntryList->next = (struct DirEntryList*)malloc(sizeof(struct DirEntryList));
			tempDirEntryList->next->prev = tempDirEntryList;
			tempDirEntryList = tempDirEntryList->next;
			tempDirEntryList->next = NULL;
			dirEntry = &(tempDirEntryList->dirEntry);
		}
		/*Goto next cluster if it is not EOF*/
		if(FAT[FATValue] < 0x0ffffff8){
			free(buffer);
			nextCluster = (FAT[FATValue]-FATValue) * bootEntry->BPB_SecPerClus * bootEntry->BPB_BytsPerSec - bytesRead;
			buffer = (char*)malloc(nextCluster);
			read(fileDescriptor, buffer, nextCluster);
		}
		FATValue = FAT[FATValue];
	}
	tempDirEntryList->prev->next = NULL;
	free(tempDirEntryList);

	free(buffer);
	close(fileDescriptor);
}

void listRootDirectory(){
	struct DirEntryList *tempDirEntryList = dirEntryList;
	struct DirEntry *tempDirEntry = &(dirEntryList->dirEntry);
	int cluster, order=1, i, end;

	while(tempDirEntryList != NULL){
		cluster = tempDirEntry->DIR_FstClusHI<<sizeof(short)|tempDirEntry->DIR_FstClusLO;

		//Print the file name if it is not deleted and in vaild filename
		if(tempDirEntry->DIR_Name[0]!=0xE5 && tempDirEntry->DIR_Name[0]!=0 && tempDirEntry->DIR_Name[0]!=' '){
			printf("%d, ", order);

			/*Search the tail of file name*/
			for(i=7; i>0; i--){
				if(tempDirEntry->DIR_Name[i] != ' '){
					end = i+1;
					break;
				}
			}

			/*Print file name*/
			for(i=0; i<end; i++)
				printf("%c", tempDirEntry->DIR_Name[i]);

			if(tempDirEntry->DIR_Name[8]!=' '){
				printf(".");
	
				/*Search the tail of extension*/
				for(i=10; i>7; i--){
					if(tempDirEntry->DIR_Name[i]!=' '){
						end = i+1;
						break;
					}
				}

				/*Print file extension*/
				for(i=8; i<end; i++)
					printf("%c", tempDirEntry->DIR_Name[i]);
			}

			/*Check if it is a directory*/
			if(tempDirEntry->DIR_Attr == (tempDirEntry->DIR_Attr|0x10))
				printf("/");

			printf(", %ld, %d\n", tempDirEntry->DIR_FileSize, cluster);
			order++;
		}
		tempDirEntryList = tempDirEntryList->next;
		tempDirEntry = &(tempDirEntryList->dirEntry);
	}
}

void checkFilename(char c){
	if( !(isupper(c) || isdigit(c)) )
		if( !(c=='$' || c=='%' || c=='\'' || c=='=' || c=='{' || c=='}' || c=='~') )
			if( !(c=='!' || c=='#' || c=='(' || c==')' || c=='&' || c=='_' || c=='^' || c==' ') )
				fileNotFoundThenExit();
}

void calculateMD5(char *md5, unsigned int firstCluster, unsigned long fileSize){
	int offsetToDataArea = ((bootEntry->BPB_NumFATs * bootEntry->BPB_FATSz32) + bootEntry->BPB_RsvdSecCnt) * bootEntry->BPB_BytsPerSec;
	int sizeOfAClusterInBytes = bootEntry->BPB_BytsPerSec * bootEntry->BPB_SecPerClus;
	int fileDescriptor, bytesRead = 0, currentCluster = firstCluster, i;
	void *fileContent;
	float numClustersSpan = fileSize / sizeOfAClusterInBytes;

	fileDescriptor = open(deviceFileName, O_RDONLY);
	fileContent = malloc(fileSize);


	while(numClustersSpan >= 0  && fileSize - bytesRead > 0){
		lseek(fileDescriptor, offsetToDataArea, SEEK_SET);
		lseek(fileDescriptor, ((currentCluster-2) * sizeOfAClusterInBytes), SEEK_CUR);
	
		if(fileSize - bytesRead > sizeOfAClusterInBytes)
			bytesRead += read(fileDescriptor, (fileContent + bytesRead), sizeOfAClusterInBytes);
		else{
			bytesRead += read(fileDescriptor, (fileContent + bytesRead), (fileSize - bytesRead));
			break;
		}
		currentCluster++;
		numClustersSpan -= 1.0f;
	}

	MD5(fileContent, fileSize, md5);
	free(fileContent);
}

bool md5Matched(unsigned char *md5){
	int i;
	bool matched = true;
	char *buf = (char*)malloc(sizeof(char) * MD5_DIGEST_LENGTH * 2);
	for(i=0; i<MD5_DIGEST_LENGTH; i++)
		sprintf(buf+i*2, "%02x", md5[i]);
	for(i=0; i<MD5_DIGEST_LENGTH*2; i++)
		if(buf[i] != targetMD5[i]){
			matched = false;
			break;
		}

	free(buf);
	return matched;
}

void fileRecovery(bool haveMD5){
	/*For filename checking*/
	int i, j, length=strlen(fileName);
	char* dirName = (char*)malloc(sizeof(char)*11);
	unsigned char* fileMD5 = (unsigned char*)malloc(MD5_DIGEST_LENGTH);
	
	/*For file recovery*/
	struct DirEntryList *tempDirEntryList = dirEntryList;
	struct DirEntry *tempDirEntry = &(dirEntryList->dirEntry);
	struct DirEntry *matchDirEntry;

	int cluster, bytsPerClus = bootEntry->BPB_BytsPerSec * bootEntry->BPB_SecPerClus;

	bool match = false;	//True when there exist a deleted file match with the input filename
	int fileNum = 1, matchCluster;
	int fileDescriptor;

	/*Filename checking*/
	if(length>12 || fileName[0] == ' ')
		fileNotFoundThenExit();

	/*Copy filename to dirName and make space alignment*/
	for(i=0, j=0; i<8; i++){
		if(j<length && fileName[j]!='.'){
			checkFilename(fileName[j]);
			dirName[i] = fileName[j];
			j++;
		}
		else
			dirName[i] = ' ';
	}
	
	if(j==8 && fileName[j]!='.')
		fileNotFoundThenExit();
	
	if(fileName[j] == '.' && fileName[j+1] == ' ')
			fileNotFoundThenExit();

	if(length-j>4)
			fileNotFoundThenExit();

	for( j++; i<11; i++){
		if(j<length){
			checkFilename(fileName[j]);
			dirName[i] = fileName[j];
			j++;
		}
		else{
			dirName[i] = ' ';
		}
	}

	/*File recovery*/
	while(tempDirEntryList->next != NULL){
		cluster = tempDirEntry->DIR_FstClusHI<<sizeof(short)|tempDirEntry->DIR_FstClusLO;

		/*Dig out all deleted file entries*/
		if(tempDirEntry->DIR_Attr|0x0F!=tempDirEntry->DIR_Attr && tempDirEntry->DIR_Name[0]==0xE5){
			/*Name compare*/
			for(i=1; i<11; i++){
				if( tempDirEntry->DIR_Name[i] != dirName[i])
					break;
			}

			/*If the file name match, test whether it is ambiguous*/
			if(i == 11){
				if(match == false){
					if(haveMD5 == true){
						calculateMD5(fileMD5, cluster, tempDirEntry->DIR_FileSize);
						if(md5Matched(fileMD5) == true){
							matchDirEntry = tempDirEntry;
							match = true;	
						}
						else
							match = false;
					}
					else{
						matchDirEntry = tempDirEntry;
						match = true;
					}
				}
				else
					ambiguousThenExit();
			}
		}
		if(match == false)
			fileNum++;
		tempDirEntryList = tempDirEntryList->next;
		tempDirEntry = &(tempDirEntryList->dirEntry);
	}
	
	/*Recover the file if we found it*/
	if(match==true){
		cluster = matchDirEntry->DIR_FstClusHI<<sizeof(short)|matchDirEntry->DIR_FstClusLO;

		//Check FAT to test whether the deleted cluster is complete or not
		for(i=cluster; i< cluster + matchDirEntry->DIR_FileSize/bytsPerClus; i++)
			if((FAT[cluster+i]!=0 || matchDirEntry->DIR_FileSize==0) && FAT[cluster+i]<0x0ffffff8)
				fileNotFoundThenExit();

		//Prepare the FAT array
		for(i=0; i<bootEntry->BPB_NumFATs; i++){
			for(j=0; j< matchDirEntry->DIR_FileSize/bytsPerClus; j++){
				FAT[i* bootEntry->BPB_FATSz32/ bootEntry->BPB_SecPerClus+ cluster+ j] = i* bootEntry->BPB_FATSz32/ bootEntry->BPB_SecPerClus+ cluster+ j+ 1;
			}
			if(matchDirEntry->DIR_FileSize != 0)
				FAT[i* bootEntry->BPB_FATSz32/ bootEntry->BPB_SecPerClus +cluster+j] = 0x0ffffff8;
			else
				FAT[i* bootEntry->BPB_FATSz32/ bootEntry->BPB_SecPerClus +cluster+j] = 0;
		}

		/*Write FAT array into FAT in file srystem*/
		fileDescriptor = open(deviceFileName, O_WRONLY);
		if(fileDescriptor < 0){
			displayUsageThenExit();}
	
		//Move file position to FAT
		lseek(fileDescriptor, bootEntry->BPB_RsvdSecCnt * bootEntry->BPB_BytsPerSec, SEEK_SET);

		//Recover FAT
		write(fileDescriptor, &FAT[0], bootEntry->BPB_NumFATs * bootEntry->BPB_FATSz32 * bootEntry->BPB_BytsPerSec);

		/*Recover file name*/
		//Move file position to root directory
		lseek(fileDescriptor, (bootEntry->BPB_RsvdSecCnt+bootEntry->BPB_NumFATs*bootEntry->BPB_FATSz32+bootEntry->BPB_RootClus-2)*bootEntry->BPB_BytsPerSec, SEEK_SET);
		
		//Move file position to the match cluster
		matchCluster = bootEntry->BPB_RootClus;
		for(i=0; i<fileNum/(bootEntry->BPB_SecPerClus*bootEntry->BPB_BytsPerSec/sizeof(struct DirEntry));i++){
			matchCluster = FAT[matchCluster];
		}
		lseek(fileDescriptor, (matchCluster-2)*bootEntry->BPB_SecPerClus*bootEntry->BPB_BytsPerSec, SEEK_CUR);

		//Move file position to the match directory entry
		lseek(fileDescriptor, (fileNum%(bootEntry->BPB_SecPerClus*bootEntry->BPB_BytsPerSec/sizeof(struct DirEntry))-1)*sizeof(struct DirEntry), SEEK_CUR);

		//Correct the file name
		write(fileDescriptor, dirName, sizeof(char));
		
		if(haveMD5 == false)
			printf("%s: recovered.\n", fileName);
		else
			printf("%s: recovered with MD5.\n", fileName);
	}
	else
		fileNotFoundThenExit();

	free(fileMD5);

	close(fileDescriptor);
}

int main(int argc, char **const argv){
	programName = argv[0];
	if(argc>1 && !strcmp(argv[1], "-d"))
		deviceFileName = argv[2];
	else
		displayUsageThenExit();

	bootEntry = (struct BootEntry*)malloc(90); /*sizeof(struct BootEntry) == 90*/
	readBootSectorInformation(bootEntry);

	/*Buffer for containing all FATs*/
	FAT = (int*)malloc(bootEntry->BPB_NumFATs*bootEntry->BPB_FATSz32*bootEntry->BPB_BytsPerSec);

	dirEntryList = (struct DirEntryList*)malloc(sizeof(struct DirEntryList));
	dirEntryList->prev = NULL;
	dirEntryList->next = NULL;
	readRootDirectory();

	if((argc <= 3) || (argc == 6) || (argc > 7))
		displayUsageThenExit();
	if(argc == 4){
		if(!strcmp(argv[3], "-i"))
			printBootSectorInformation();
		else if(!strcmp(argv[3], "-l"))
			listRootDirectory();
		else
			displayUsageThenExit();
	}
	if(argc == 5){
		if(!strcmp(argv[3], "-r")){
			fileName = argv[4];
			fileRecovery(false);
		}
		else
			displayUsageThenExit();
	}
	if(argc == 7){
		if(!strcmp(argv[3], "-r"))
			fileName = argv[4];
		else
			displayUsageThenExit();
		if(!strcmp(argv[5], "-m")){
			targetMD5 = argv[6];
			fileRecovery(true);
		}
		else
			displayUsageThenExit();
	}

	free(FAT);
	free(bootEntry);
	return 0; 
}
