#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include "libdisksimul.h"
#include "filesystem.h"

#define MAX_ROOT_ENTRIES 15
#define MAX_DIR_ENTRIES 16


/**
 * @brief Verify if dir exist and return its sector
 * @param t_dir table_directory pointer.
 * @param s_path dir path.
 * @param cur_entries root entries (then used for current entries).
 * @return dir sector number or -1 if not found.
 */
int find_dir(struct table_directory *t_dir, char *s_path, struct file_dir_entry *cur_entries){
	int exists = 0;
	int i = 0;
	int s_dir;
	printf("- Searching path: %s \n", s_path);
	const char delimiter[2] = "/";
	char *e_name = strtok(s_path, delimiter);
	int t =  0;

	// Verify if path exists and navigate through
	while( e_name != NULL ) 
	{	
		printf("- Searching dir: %s \n", e_name);
		exists = 0;
		
		//verify if	dir exist on current entries
		for(i; i < MAX_DIR_ENTRIES; i++){
			if(strcmp(cur_entries[i].name, e_name) == 0 && cur_entries[i].dir == 1){
				exists = 1;

				// go to next path segment
				//e_name = strtok(NULL, delimiter);

				//read dir sector
				s_dir = cur_entries[i].sector_start;
				ds_read_sector(s_dir, (void*)&t_dir, SECTOR_SIZE);

				//set next entries from dir
				cur_entries = t_dir->entries;

				break;
			}
		}

		e_name = strtok(NULL, delimiter);

		if(!exists){
			printf("Error: The path doesn't exist\n");
			return -1;
		}

		i = 0;
		exists = 0;
	}

	return s_dir;
}

/**
 * @brief Format disk.
 * 
 */
int fs_format(){
	int ret, i;
	struct root_table_directory root_dir;
	struct sector_data sector;
	
	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 1)) != 0 ){
		return ret;
	}
	
	memset(&root_dir, 0, sizeof(root_dir));
	
	root_dir.free_sectors_list = 1; /* first free sector. */
	
	ds_write_sector(0, (void*)&root_dir, SECTOR_SIZE);
	
	/* Create a list of free sectors. */
	memset(&sector, 0, sizeof(sector));
	
	for(i=1;i<NUMBER_OF_SECTORS;i++){
		if(i<NUMBER_OF_SECTORS-1){
			sector.next_sector = i+1;
		}else{
			sector.next_sector = 0;
		}
		ds_write_sector(i, (void*)&sector, SECTOR_SIZE);
	}
	
	ds_stop();
	
	printf("Disk size %d kbytes, %d sectors.\n", (SECTOR_SIZE*NUMBER_OF_SECTORS)/1024, NUMBER_OF_SECTORS);
	
	return 0;
}

/**
 * @brief Create a new file on the simulated filesystem.
 * @param input_file Source file path.
 * @param simul_file Destination file path on the simulated file system.
 * @return 0 on success.
 */
int fs_create(char* input_file, char* simul_file){
	int ret;
	
	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0 ){
		return ret;
	}

	/* Write the code to load a new file to the simulated filesystem. */
	printf("- Creating '%s' at '%s'\n", input_file, simul_file);
	
	/* initiate base */
	struct sector_data sector;
	struct root_table_directory root_dir;
	ds_read_sector(0, (void*)&root_dir, SECTOR_SIZE);

	/* set path */
	char *s_name = strdup(basename(simul_file));
	char *s_path = strdup(dirname(simul_file));
	char *str = strdup(dirname(simul_file));	
	
	const char delimiter[2] = "/";
	char *e_name = strtok(str, delimiter);

	int i = 0;
	int isRoot = 1;
	int length;
	struct file_dir_entry* cur_entries;
	struct file_dir_entry cur_entry;
	int s_dir;
	struct table_directory t_dir;
	cur_entries = root_dir.entries;

	/* open file */
	FILE *fileptr;
	char *buffer;
	long filelen;

	/* amount data for current sector */ 
	int data_amount = 0;
	/* sector number for data */
	int sector_number;

	/* file info */
	fileptr = fopen(input_file, "rb");
	fseek(fileptr, 0, SEEK_END);
	filelen = ftell(fileptr);
	rewind(fileptr);

	// is not root, search dir
	if( e_name != NULL ) {
		isRoot = 0;
		if((s_dir = find_dir(&t_dir, s_path, cur_entries)) < 1){
			return 1;
		}

		length = MAX_DIR_ENTRIES;
		cur_entries = t_dir.entries;
	}else{
		length = MAX_ROOT_ENTRIES;
		cur_entries = root_dir.entries;
	}

	// Verify which is the next free entry position
	for(i=0; i < length; i++){
		if(strcmp(cur_entries[i].name, s_name) == 0 && cur_entries[i].dir == 0){
			printf("Error: Already exist a file with the same name\n");
			return 1;
		}

		if(cur_entries[i].sector_start == 0){
			break;
		}

		// if didnt break, all slots are in use
		if(i == length - 1){
			printf("Error: Cant write anymore at this dir\n");
			return 1;
		}
	}

	// set entry file
	cur_entries[i].dir = 0;
	strcpy(cur_entries[i].name, s_name);
	cur_entries[i].sector_start = root_dir.free_sectors_list;
	cur_entries[i].size_bytes = filelen;	

	if(isRoot){
		root_dir.entries[i] = cur_entries[i];
	}else{
		t_dir.entries[i] = cur_entries[i];
		ds_write_sector(s_dir, (void*)&t_dir, SECTOR_SIZE);
	}

	/* set sector to the first free */
	memset(&sector, root_dir.free_sectors_list, sizeof(sector));
	sector_number = root_dir.free_sectors_list;

	printf("File data starts at: %d\n", cur_entries[i].sector_start);

	
	while(1){

		// have read all file
		if( ftell(fileptr) == filelen ){
			break;
		}

		// set data to sector
		sprintf(sector.data, "%d", fileptr);

		// have more than 508 bytes to read
		if(ftell(fileptr) + 508 < filelen){
			data_amount = 508;
			sector.next_sector = sector_number + 1;
		}
		// have less than 508 bytes to read
		else{
			data_amount = filelen - ftell(fileptr);
			sector.next_sector = 0;
			root_dir.free_sectors_list = sector_number + 1;
			ds_write_sector(0, (void*)&root_dir, SECTOR_SIZE);
		}
		
		// move the file pointer for the data_amount available at this iteration
		fseek(fileptr, data_amount, SEEK_CUR);
		printf("%d: data_amount: %d\n", sector_number, data_amount);
		//printf("offset: %d\n", ftell(fileptr));
		ds_write_sector(sector_number++, (void*)&sector, SECTOR_SIZE);					
	}

	// save root_dir current context
	ds_write_sector(0, (void*)&root_dir, SECTOR_SIZE);

	printf("free sector: %d\n", root_dir.free_sectors_list);

	fclose(fileptr);	
	ds_stop();
	
	return 0;
}

/**
 * @brief Read file from the simulated filesystem.
 * @param output_file Output file path.
 * @param simul_file Source file path from the simulated file system.
 * @return 0 on success.
 */
int fs_read(char* output_file, char* simul_file){
	int ret;
	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0 ){
		return ret;
	}
	
	/* Write the code to read a file from the simulated filesystem. */
	
	ds_stop();
	
	return 0;
}

/**
 * @brief Delete file from file system.
 * @param simul_file Source file path.
 * @return 0 on success.
 */
int fs_del(char* simul_file){
	int ret;
	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0 ){
		return ret;
	}
	
	/* Write the code delete a file from the simulated filesystem. */
	
	ds_stop();
	
	return 0;
}

/**
 * @brief List files from a directory.
 * @param simul_file Source file path.
 * @return 0 on success.
 */
int fs_ls(char *dir_path){
	int ret;
	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0 ){
		return ret;
	}
	
	/* Write the code to show files or directories. */
	
	ds_stop();
	
	return 0;
}

/**
 * @brief Create a new directory on the simulated filesystem.
 * @param directory_path directory path.
 * @return 0 on success.
 */
int fs_mkdir(char* directory_path){
	int ret;
	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0 ){
		return ret;
	}
	
	/* Write the code to create a new directory. */
	
	ds_stop();
	
	return 0;
}

/**
 * @brief Remove directory from the simulated filesystem.
 * @param directory_path directory path.
 * @return 0 on success.
 */
int fs_rmdir(char *directory_path){
	int ret;
	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0 ){
		return ret;
	}
	
	/* Write the code to delete a directory. */
	
	ds_stop();
	
	return 0;
}

/**
 * @brief Generate a map of used/available sectors. 
 * @param log_f Log file with the sector map.
 * @return 0 on success.
 */
int fs_free_map(char *log_f){
	int ret, i, next;
	struct root_table_directory root_dir;
	struct sector_data sector;
	char *sector_array;
	FILE* log;
	int pid, status;
	int free_space = 0;
	char* exec_params[] = {"gnuplot", "sector_map.gnuplot" , NULL};

	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0 ){
		return ret;
	}
	
	/* each byte represents a sector. */
	sector_array = (char*)malloc(NUMBER_OF_SECTORS);

	/* set 0 to all sectors. Zero means that the sector is used. */
	memset(sector_array, 0, NUMBER_OF_SECTORS);
	
	/* Read the root dir to get the free blocks list. */
	ds_read_sector(0, (void*)&root_dir, SECTOR_SIZE);
	
	next = root_dir.free_sectors_list;

	while(next){
		/* The sector is in the free list, mark with 1. */
		sector_array[next] = 1;
		
		/* move to the next free sector. */
		ds_read_sector(next, (void*)&sector, SECTOR_SIZE);
		
		next = sector.next_sector;
		
		free_space += SECTOR_SIZE;
	}

	/* Create a log file. */
	if( (log = fopen(log_f, "w")) == NULL){
		perror("fopen()");
		free(sector_array);
		ds_stop();
		return 1;
	}
	
	/* Write the the sector map to the log file. */
	for(i=0;i<NUMBER_OF_SECTORS;i++){
		if(i%32==0) fprintf(log, "%s", "\n");
		fprintf(log, " %d", sector_array[i]);
	}
	
	fclose(log);
	
	/* Execute gnuplot to generate the sector's free map. */
	pid = fork();
	if(pid==0){
		execvp("gnuplot", exec_params);
	}
	
	wait(&status);
	
	free(sector_array);
	
	ds_stop();
	
	printf("Free space %d kbytes.\n", free_space/1024);
	
	return 0;
}

