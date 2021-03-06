#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <assert.h>
#include "parse.h"
#include "userfs.h"
#include "crash.h"

/* GLOBAL  VARIABLES */
int virtual_disk;
superblock sb;  
BIT bit_map[BIT_MAP_SIZE];
dir_struct dir;

inode curr_inode;
char buffer[BLOCK_SIZE_BYTES]; /* assert( sizeof(char) ==1)); */

/*
  man 2 read
  man stat
  man memcopy
*/


void usage (char * command) 
{
	fprintf(stderr, "Usage: %s -reformat disk_size_bytes file_name\n", 
		command);
	fprintf(stderr, "        %s file_ame\n", command);
}

char * buildPrompt()
{
	return  "%";
}


int main(int argc, char** argv)
{

	char * cmd_line;
	/* info stores all the information returned by parser */
	parseInfo *info; 
	/* stores cmd name and arg list for one command */
	struct commandType *cmd;

  
	init_crasher();

	if ((argc == 4) && (argv[1][1] == 'r'))
	{
		/* ./userfs -reformat diskSize fileName */
		if (!u_format(atoi(argv[2]), argv[3])){
			fprintf(stderr, "Unable to reformat\n");
			exit(-1);
		}
	}  else if (argc == 2)  {
   
		/* ./userfs fileName will attempt to recover a file. */
		if ((!recover_file_system(argv[1])) )
		{
			fprintf(stderr, "Unable to recover virtual file system from file: %s\n",
				argv[1]);
			exit(-1);
		}
	}  else  {
		usage(argv[0]);
		exit(-1);
	}
  
  
	/* before begin processing set clean_shutdown to FALSE */
	sb.clean_shutdown = 0;
	lseek(virtual_disk, BLOCK_SIZE_BYTES* SUPERBLOCK_BLOCK, SEEK_SET);
	crash_write(virtual_disk, &sb, sizeof(superblock));  
	sync();
	fprintf(stderr, "userfs available\n");

	while(1) { 

		cmd_line = readline(buildPrompt());
		if (cmd_line == NULL) {
			fprintf(stderr, "Unable to read command\n");
			continue;
		}

  
		/* calls the parser */
		info = parse(cmd_line);
		if (info == NULL){
			free(cmd_line); 
			continue;
		}

		/* com contains the info. of command before the first "|" */
		cmd=&info->CommArray[0];
		if ((cmd == NULL) || (cmd->command == NULL)){
			free_info(info); 
			free(cmd_line); 
			continue;
		}
  
		/************************   u_import ****************************/
		if (strncmp(cmd->command, "u_import", strlen("u_import")) ==0){

			if (cmd->VarNum != 3){
				fprintf(stderr, 
					"u_import externalFileName userfsFileName\n");
			} else {
				if (!u_import(cmd->VarList[1], 
					      cmd->VarList[2]) ){
					fprintf(stderr, 
						"Unable to import external file %s into userfs file %s\n",
						cmd->VarList[1], cmd->VarList[2]);
				}
			}
     

			/************************   u_export ****************************/
		} else if (strncmp(cmd->command, "u_export", strlen("u_export")) ==0){


			if (cmd->VarNum != 3){
				fprintf(stderr, 
					"u_export userfsFileName externalFileName \n");
			} else {
				if (!u_export(cmd->VarList[1], cmd->VarList[2]) ){
					fprintf(stderr, 
						"Unable to export userfs file %s to external file %s\n",
						cmd->VarList[1], cmd->VarList[2]);
				}
			}


			/************************   u_del ****************************/
		} else if (strncmp(cmd->command, "u_del", 
				   strlen("u_export")) ==0){
			
			if (cmd->VarNum != 2){
				fprintf(stderr, "u_del userfsFileName \n");
			} else {
				if (!u_del(cmd->VarList[1]) ){
					fprintf(stderr, 
						"Unable to delete userfs file %s\n",
						cmd->VarList[1]);
				}
			}


       
			/******************** u_ls **********************/
		} else if (strncmp(cmd->command, "u_ls", strlen("u_ls")) ==0){
			u_ls();


			/********************* u_quota *****************/
		} else if (strncmp(cmd->command, "u_quota", strlen("u_quota")) ==0){
			int free_blocks = u_quota();
			fprintf(stderr, "Free space: %d bytes %d blocks\n", 
				free_blocks* BLOCK_SIZE_BYTES, 
				free_blocks);


			/***************** exit ************************/
		} else if (strncmp(cmd->command, "exit", strlen("exit")) ==0){
			/* 
			 * take care of clean shut down so that u_fs
			 * recovers when started next.
			 */
			if (!u_clean_shutdown()){
				fprintf(stderr, "Shutdown failure, possible corruption of userfs\n");
			}
			exit(1);


			/****************** other ***********************/
		}else {
			fprintf(stderr, "Unknown command: %s\n", cmd->command);
			fprintf(stderr, "\tTry: u_import, u_export, u_ls, u_del, u_quota, exit\n");
		}

     
		free_info(info);
		free(cmd_line);
	}	      
  
}

/*
 * Initializes the bit map.
 */
void
init_bit_map()
{
	int i;
	for (i=0; i< BIT_MAP_SIZE; i++)
	{
		bit_map[i] = 0;
	}

}

void
allocate_block(int blockNum)
{
	assert(blockNum < BIT_MAP_SIZE);
	bit_map[blockNum]= 1;
}

void
free_block(int blockNum)
{
	assert(blockNum < BIT_MAP_SIZE);
	bit_map[blockNum]= 0;
}

int
superblockMatchesCode()
{
	if (sb.size_of_super_block != sizeof(superblock)){
		return 0;
	}
	if (sb.size_of_directory != sizeof (dir_struct)){
		return 0;
	}
	if (sb.size_of_inode != sizeof(inode)){
		return 0;
	}
	if (sb.block_size_bytes != BLOCK_SIZE_BYTES){
		return 0;
	}
	if (sb.max_file_name_size != MAX_FILE_NAME_SIZE){
		return 0;
	}
	if (sb.max_blocks_per_file != MAX_BLOCKS_PER_FILE){
		return 0;
	}
	return 1;
}

void
init_superblock(int diskSizeBytes)
{
	sb.disk_size_blocks  = diskSizeBytes/BLOCK_SIZE_BYTES;
	sb.num_free_blocks = u_quota();
	sb.clean_shutdown = 1;

	sb.size_of_super_block = sizeof(superblock);
	sb.size_of_directory = sizeof (dir_struct);
	sb.size_of_inode = sizeof(inode);

	sb.block_size_bytes = BLOCK_SIZE_BYTES;
	sb.max_file_name_size = MAX_FILE_NAME_SIZE;
	sb.max_blocks_per_file = MAX_BLOCKS_PER_FILE;
}

int 
compute_inode_loc(int inode_number)
{
	int whichInodeBlock;
	int whichInodeInBlock;
	int inodeLocation;

	whichInodeBlock = inode_number/INODES_PER_BLOCK;
	whichInodeInBlock = inode_number%INODES_PER_BLOCK;
  
	inodeLocation = (INODE_BLOCK + whichInodeBlock) *BLOCK_SIZE_BYTES +
		whichInodeInBlock*sizeof(inode);
  
	return inodeLocation;
}
int
write_inode(int inode_number, inode * in)
{

	int inodeLocation;
	assert(inode_number < MAX_INODES);

	inodeLocation = compute_inode_loc(inode_number);
  
	lseek(virtual_disk, inodeLocation, SEEK_SET);
	crash_write(virtual_disk, in, sizeof(inode));
  
	sync();

	return 1;
}


int
read_inode(int inode_number, inode * in)
{
	int inodeLocation;
	assert(inode_number < MAX_INODES);

	inodeLocation = compute_inode_loc(inode_number);

  
	lseek(virtual_disk, inodeLocation, SEEK_SET);
	read(virtual_disk, in, sizeof(inode));
  
	return 1;
}
	

/*
 * Initializes the directory.
 */
void
init_dir()
{
	int i;
	for (i=0; i< MAX_FILES_PER_DIRECTORY; i++)
	{
		dir.u_file[i].free = 1;
	}

}




/*
 * Returns the no of free blocks in the file system.
 */
int u_quota()
{

	int freeCount=0;
	int i;


	/* if you keep sb.num_free_blocks up to date can just
	   return that!!! */


	/* that code is not there currently so...... */

	/* calculate the no of free blocks */
	for (i=0; i < sb.disk_size_blocks; i++ )
	{

		/* right now we are using a full unsigned int
		   to represent each bit - we really should use
		   all the bits in there for more efficient storage */
		if (bit_map[i]==0)
		{
			freeCount++;
		}
	}
	return freeCount;
}

/*
 * Imports a linux file into the u_fs
 * Need to take care in the order of modifying the data structures 
 * so that it can be revored consistently.
 */
int u_import(char* linux_file, char* u_file)
{
	int free_space;
	int free_space_in_bytes;
	free_space = u_quota();
	free_space_in_bytes = free_space * BLOCK_SIZE_BYTES;

	// Get around this assignment's silly syntax
	int true = TRUE;
	int false = FALSE;

	int handle = open(linux_file,O_RDONLY);
	if ( -1 == handle ) {
		printf("error, reading file %s\n",linux_file);
		return 0;
	}

	//read(handle,&buffer,BLOCK_SIZE_BYTES);

	//crash_write(virtual_disk, &buffer, 1999 );
  

	/* write rest of code for importing the file.
	   return 1 for success, 0 for failure */

	/* Process:
		-Get the file. *Done
		-Make sure the file's name fits with our naming conventions *Done
			-Return 1 if fail *Done
		-Get the size of the file *Done
		-Compare it against current space available in file system *Done
		-Check to see if file can be represented w/ MAX_BLOCKS_PER_FILE *Done
		-Make sure we haven't consumed all inodes
		-Make sure the directory doesn't have too many files
		-Check to see if a file with that name exists already
		-Dump all of the information from that file into an internal buffer.
		-Update all the things!
			-bitmap, directory, inode, datablocks, superblock(?)
			-Ordering? Partial operation on crash?
				-datablock (orphan if crash), then bitmap, then inode, 
			     directory, superblock
	*/

	// Check to see if the intended file name fits out conventions
	if(strlen(u_file) > MAX_FILE_NAME_SIZE || strlen(u_file) == 0){
		printf("\nError: Intended UFS file name is invalid!\n");
		return 0;
	}	

	struct stat stbuf; // Our file info structure
	
	// Get file info
	if (stat(linux_file, &stbuf) == -1){
		printf("\n Error: File not found in the linux file system!\n");
		return 0;
	}

	off_t file_size = stbuf.st_size;

	// Make sure there's enough space on the UFS to hold the file
	if (file_size > free_space_in_bytes){
		printf("\n Error: Not enough room in UFS! \n");	
		return 0;
	}

	// Make sure the file does not exceed our max block size per file
	if ((file_size/BLOCK_SIZE_BYTES) > MAX_BLOCKS_PER_FILE){
		printf("\n Error: File is too large to fit in max block size!\n");
		return 0;
	}

	// Check to see if a file of the same name exists already
	for (int i = 0; i < MAX_FILES_PER_DIRECTORY; i++){
		if (!(dir.u_file[i].free) && strcmp(dir.u_file[i].file_name, u_file) == 0){
			printf("\n Error: File already exists in UFS!\n");
			return 0;
		}
	}

	// Get first free file index
	int free_file_idx = -1;
	for (int i = 0; i < MAX_FILES_PER_DIRECTORY; i++){
		if(dir.u_file[i].free){
			free_file_idx = i;
			break;
		}
	}

	// Make sure we got a free file index
	if (free_file_idx == -1){
		printf("\nError: UFS is at max file cap!\n");
		return 0;
	}

	// Additional sanity check: Make sure we didn't somehow get past earlier check w/ no free files
	if (dir.no_files >= MAX_FILES_PER_DIRECTORY){
		printf("\nError: UFS has no available files!\n");
		return 0;
	}

	// Check to see if we have an available inode
	int inode_idx;
	for(int i = 0; i <= MAX_INODES; i++){
		read_inode(i, &curr_inode);
		if(curr_inode.free == true){
			inode_idx = i;
			break;
		}else if(inode_idx == MAX_INODES){
			printf("\n Error: No free inodes in UFS!\n");
			return 0;
		}
	}

	// Alright! Should be guaranteed to write a file now.

	dir.u_file[free_file_idx].inode_number = inode_idx;
	strncpy(dir.u_file[free_file_idx].file_name, u_file, MAX_FILE_NAME_SIZE);
	dir.u_file[free_file_idx].free = false;
	
	curr_inode.no_blocks = ((stbuf.st_size +(BLOCK_SIZE_BYTES - 1)) / BLOCK_SIZE_BYTES); 	
	curr_inode.file_size_bytes = stbuf.st_size;
	curr_inode.free = false;

	dir.no_files = dir.no_files + 1;

	int block = 0;
	for (int i = 0; i < curr_inode.no_blocks; i++){
		if(bit_map[i] == 0){
			allocate_block(i);
			curr_inode.blocks[block] = i;
			block++;
			if (block >= curr_inode.no_blocks){
				break;
			}
		}
	}

	for (int i = 0; i < curr_inode.no_blocks; i++){
		read(handle, &buffer, BLOCK_SIZE_BYTES);
		lseek(virtual_disk, BLOCK_SIZE_BYTES * curr_inode.blocks[i], SEEK_SET);
		crash_write(virtual_disk, &buffer, sizeof(buffer));
	}

	close(handle);

	lseek(virtual_disk, BIT_MAP_BLOCK * BLOCK_SIZE_BYTES, SEEK_SET);
	crash_write(virtual_disk, bit_map, sizeof(bit_map));
	
	write_inode(inode_idx, &curr_inode);	

	lseek(virtual_disk, DIRECTORY_BLOCK * BLOCK_SIZE_BYTES, SEEK_SET);
	crash_write(virtual_disk, &dir, sizeof(dir));
	
	lseek(virtual_disk, SUPERBLOCK_BLOCK * BLOCK_SIZE_BYTES, SEEK_SET);
	crash_write(virtual_disk, &sb, sizeof(sb));
	
 
	return 1;
}



/*
 * Exports a u_file to linux.
 * Need to take care in the order of modifying the data structures 
 * so that it can be revored consistently.
 */
int u_export(char* u_file, char* linux_file)
{
	/*
	  write code for exporting a file to linux.
	  return 1 for success, 0 for failure

	  check ok to open external file for writing

	  check userfs file exists

	  read the data out of ufs and write it into the external file
	*/
	
	int handle = open(linux_file,O_WRONLY|O_CREAT|O_TRUNC,S_IRGRP|S_IWGRP|S_IRUSR|S_IWUSR);
	if ( -1 == handle ) {
		printf("error, reading file %s\n",linux_file);
		return 0;
	}

	for (int i = 0; i < MAX_FILES_PER_DIRECTORY; i++){
		if(!(dir.u_file[i].free) && strcmp(dir.u_file[i].file_name, u_file) == 0){
			read_inode(dir.u_file[i].inode_number, &curr_inode);
			break;
		}else if(i == (MAX_FILES_PER_DIRECTORY -1)){
			printf("\nError: The requested UFS file for export was not found!\n");
			return 0;
		}
	}

	int to_read = curr_inode.file_size_bytes;
	int been_read;
	for(int i = 0; i < curr_inode.no_blocks; i++){
		lseek(virtual_disk, curr_inode.blocks[i] * BLOCK_SIZE_BYTES, SEEK_SET);
		if(to_read >= BLOCK_SIZE_BYTES){
			been_read = read(virtual_disk, buffer, BLOCK_SIZE_BYTES);
			to_read = to_read - been_read;
		} else {
			been_read = read(virtual_disk, buffer, to_read);
			to_read = to_read - been_read;
		}
		write(handle, buffer, been_read);
	}

	sync();
	close(handle);
	
	
	return 1; 
}


/*
 * Deletes the file from u_fs
 */
int u_del(char* u_file)
{
	/*
	  Write code for u_del.
	  return 1 for success, 0 for failure

	  check user fs file exists

	  update bitmap, inode, directory - in what order???

	  superblock only has to be up-to-date on clean shutdown?
	*/

	int file_idx;
	for (file_idx = 0; file_idx < MAX_FILES_PER_DIRECTORY; file_idx++){
		if(!(dir.u_file[file_idx].free) && strcmp(dir.u_file[file_idx].file_name, u_file) == 0){
			read_inode(dir.u_file[file_idx].inode_number, &curr_inode);
			break;
		}else if(file_idx == (MAX_FILES_PER_DIRECTORY-1)){
			printf("\nError: The requested UFS file for deletion was not found!\n");
			return 0;
		}
	}

	dir.no_files = dir.no_files - 1;

	dir.u_file[file_idx].free = TRUE;
	strncpy(dir.u_file[file_idx].file_name, "", MAX_FILE_NAME_SIZE);

	curr_inode.file_size_bytes = 0;
	curr_inode.free = TRUE;

	for(int i  = 0; i < curr_inode.no_blocks; i++){
		free_block(curr_inode.blocks[i]);
	}	

	write_inode(dir.u_file[file_idx].inode_number, &curr_inode);

	return 1;
}

/*
 * Checks the file system for consistency.
 */
int u_fsck()
{
	/*
	  Write code for u_fsck.
	  return 1 for success, 0 for failure

	  any inodes maked taken not pointed to by the directory?
	  
	  are there any things marked taken in bit map not
	  pointed to by a file?
	*/

	for (int i = 0; i < MAX_INODES; i++){
		read_inode(i, &curr_inode);
		if(curr_inode.free == 0){
			for (int j = 0; j < MAX_FILES_PER_DIRECTORY; j++){
				if(dir.u_file[j].inode_number == i){
					break;
				}else if(j == (MAX_FILES_PER_DIRECTORY-1)){
					printf("\nError: Orphaned inode found in UFS! UFS is inconsistent!\n");
					return 0;
				}
			}
		}
		
		int found = FALSE;
		for (int i = (INODE_BLOCK + NUM_INODE_BLOCKS); i < sb.disk_size_blocks; i++){
			if(bit_map[i] == 1){
				for(int j = 0; j < MAX_INODES; j++){
					read_inode(j, &curr_inode);
					for(int k = 0; k < curr_inode.no_blocks; k++){
						if(curr_inode.blocks[k] == i){
							j = MAX_INODES;
							found = TRUE;
							break;
						}
					}
				}
				if (found == FALSE){
					printf("\nEWarning: A Block was found that is allocated, but not pointed to by any inode!\n");
					printf("\nClearing it...\n");
					free_block(i);
				}
			}
		}
	}

	lseek(virtual_disk, SUPERBLOCK_BLOCK * BLOCK_SIZE_BYTES, SEEK_SET);
	write(virtual_disk, &sb, sizeof(sb));
	lseek(virtual_disk, BIT_MAP_BLOCK * BLOCK_SIZE_BYTES, SEEK_SET);
	write(virtual_disk, bit_map, sizeof(bit_map));
	lseek(virtual_disk, DIRECTORY_BLOCK * BLOCK_SIZE_BYTES, SEEK_SET);
	write(virtual_disk, &dir, sizeof(dir));
	sync();

	return 1;

}
/*
 * Iterates through the directory and prints the 
 * file names, size and last modified date and time.
 */
void u_ls()
{
	int i;
	struct tm *loc_tm;
	int numFilesFound = 0;

	for (i=0; i< MAX_FILES_PER_DIRECTORY ; i++)
	{
		if (!(dir.u_file[i].free))
		{
			numFilesFound++;
			/* file_name size last_modified */
			
			read_inode(dir.u_file[i].inode_number, &curr_inode);
			loc_tm = localtime(&curr_inode.last_modified);
			fprintf(stderr,"%s\t%d\t%d/%d\t%d:%d\n",dir.u_file[i].file_name, 
				curr_inode.no_blocks*BLOCK_SIZE_BYTES, 
				loc_tm->tm_mon, loc_tm->tm_mday, loc_tm->tm_hour, loc_tm->tm_min);
      
		}  
	}

	if (numFilesFound == 0){
		fprintf(stdout, "Directory empty\n");
	}

}

/*
 * Formats the virtual disk. Saves the superblock
 * bit map and the single level directory.
 */
int u_format(int diskSizeBytes, char* file_name)
{
	int i;
	int minimumBlocks;

	/* create the virtual disk */
	if ((virtual_disk = open(file_name, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR)) < 0)
	{
		fprintf(stderr, "Unable to create virtual disk file: %s\n", file_name);
		return 0;
	}


	fprintf(stderr, "Formatting userfs of size %d bytes with %d block size in file %s\n",
		diskSizeBytes, BLOCK_SIZE_BYTES, file_name);

	minimumBlocks = 3+ NUM_INODE_BLOCKS+1;
	if (diskSizeBytes/BLOCK_SIZE_BYTES < minimumBlocks){
		/* 
		 *  if can't have superblock, bitmap, directory, inodes 
		 *  and at least one datablock then no point
		 */
		fprintf(stderr, "Minimum size virtual disk is %d bytes %d blocks\n",
			BLOCK_SIZE_BYTES*minimumBlocks, minimumBlocks);
		fprintf(stderr, "Requested virtual disk size %d bytes results in %d bytes %d blocks of usable space\n",
			diskSizeBytes, BLOCK_SIZE_BYTES*minimumBlocks, minimumBlocks);
		return 0;
	}


	/*************************  BIT MAP **************************/

	assert(sizeof(BIT)* BIT_MAP_SIZE <= BLOCK_SIZE_BYTES);
	fprintf(stderr, "%d blocks %d bytes reserved for bitmap (%d bytes required)\n", 
		1, BLOCK_SIZE_BYTES, sizeof(BIT)* BIT_MAP_SIZE );
	fprintf(stderr, "\tImplies Max size of disk is %d blocks or %d bytes\n",
		BIT_MAP_SIZE, BIT_MAP_SIZE*BLOCK_SIZE_BYTES);
  
	if (diskSizeBytes >= BIT_MAP_SIZE* BLOCK_SIZE_BYTES){
		fprintf(stderr, "Unable to format a userfs of size %d bytes\n",
			diskSizeBytes);
		return 0;
	}

	init_bit_map();
  
	/* first three blocks will be taken with the 
	   superblock, bitmap and directory */
	allocate_block(BIT_MAP_BLOCK);
	allocate_block(SUPERBLOCK_BLOCK);
	allocate_block(DIRECTORY_BLOCK);
	/* next NUM_INODE_BLOCKS will contain inodes */
	for (i=3; i< 3+NUM_INODE_BLOCKS; i++){
		allocate_block(i);
	}
  
	lseek(virtual_disk, BLOCK_SIZE_BYTES*BIT_MAP_BLOCK, SEEK_SET);
	crash_write(virtual_disk, bit_map, sizeof(BIT)*BIT_MAP_SIZE );



	/***********************  DIRECTORY  ***********************/
	assert(sizeof(dir_struct) <= BLOCK_SIZE_BYTES);

	fprintf(stderr, "%d blocks %d bytes reserved for the userfs directory (%d bytes required)\n", 
		1, BLOCK_SIZE_BYTES, sizeof(dir_struct));
	fprintf(stderr, "\tMax files per directory: %d\n",
		MAX_FILES_PER_DIRECTORY);
	fprintf(stderr,"Directory entries limit filesize to %d characters\n",
		MAX_FILE_NAME_SIZE);

	init_dir();
	lseek(virtual_disk, BLOCK_SIZE_BYTES* DIRECTORY_BLOCK, SEEK_SET);
	crash_write(virtual_disk, &dir, sizeof(dir_struct));

	/***********************  INODES ***********************/
	fprintf(stderr, "userfs will contain %d inodes (directory limited to %d)\n",
		MAX_INODES, MAX_FILES_PER_DIRECTORY);
	fprintf(stderr,"Inodes limit filesize to %d blocks or %d bytes\n",
		MAX_BLOCKS_PER_FILE, 
		MAX_BLOCKS_PER_FILE* BLOCK_SIZE_BYTES);

	curr_inode.free = 1;
	for (i=0; i< MAX_INODES; i++){
		write_inode(i, &curr_inode);
	}

	/***********************  SUPERBLOCK ***********************/
	assert(sizeof(superblock) <= BLOCK_SIZE_BYTES);
	fprintf(stderr, "%d blocks %d bytes reserved for superblock (%d bytes required)\n", 
		1, BLOCK_SIZE_BYTES, sizeof(superblock));
	init_superblock(diskSizeBytes);
	fprintf(stderr, "userfs will contain %d total blocks: %d free for data\n",
		sb.disk_size_blocks, sb.num_free_blocks);
	fprintf(stderr, "userfs contains %d free inodes\n", MAX_INODES);
	  
	lseek(virtual_disk, BLOCK_SIZE_BYTES* SUPERBLOCK_BLOCK, SEEK_SET);
	crash_write(virtual_disk, &sb, sizeof(superblock));
	sync();


	/* when format complete there better be at 
	   least one free data block */
	assert( u_quota() >= 1);
	fprintf(stderr,"Format complete!\n");

	return 1;
} 

/*
 * Attempts to recover a file system given the virtual disk name
 */
int recover_file_system(char *file_name)
{

	if ((virtual_disk = open(file_name, O_RDWR)) < 0)
	{
		printf("virtual disk open error\n");
		return 0;
	}

	/* read the superblock */
	lseek(virtual_disk, BLOCK_SIZE_BYTES* SUPERBLOCK_BLOCK, SEEK_SET);
	read(virtual_disk, &sb, sizeof(superblock));

	/* read the bit_map */
	lseek(virtual_disk, BLOCK_SIZE_BYTES*BIT_MAP_BLOCK, SEEK_SET);
	read(virtual_disk, bit_map, sizeof(BIT)*BIT_MAP_SIZE );

	/* read the single level directory */
	lseek(virtual_disk, BLOCK_SIZE_BYTES* DIRECTORY_BLOCK, SEEK_SET);
	read(virtual_disk, &dir, sizeof(dir_struct));

	if (!superblockMatchesCode()){
		fprintf(stderr,"Unable to recover: userfs appears to have been formatted with another code version\n");
		return 0;
	}
	if (!sb.clean_shutdown)
	{
		/* Try to recover your file system */
		fprintf(stderr, "u_fsck in progress......");
		if (u_fsck()){
			fprintf(stderr, "Recovery complete\n");
			return 1;
		}else {
			fprintf(stderr, "Recovery failed\n");
			return 0;
		}
	}
	else{
		fprintf(stderr, "Clean shutdown detected\n");
		return 1;
	}
}


int u_clean_shutdown()
{
	/* write code for cleanly shutting down the file system
	   return 1 for success, 0 for failure */
  
	sb.num_free_blocks = u_quota();
	sb.clean_shutdown = 0;

	lseek(virtual_disk, BLOCK_SIZE_BYTES* SUPERBLOCK_BLOCK, SEEK_SET);
	crash_write(virtual_disk, &sb, sizeof(superblock));
	sync();

	close(virtual_disk);
	/* is this all that needs to be done on clean shutdown? */
	return !sb.clean_shutdown;
}
