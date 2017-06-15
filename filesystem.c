#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include "libdisksimul.h"
#include "filesystem.h"

#define MAX_ROOT_ENTRIES 15
#define MAX_DIR_ENTRIES 16
#define SECTOR_DATA_SIZE 508


/**
 * @brief Verify if dir exist and return its sector
 * @param t_dir table_directory pointer.
 * @param s_path dir path.
 * @param cur_entries root entries (then used for current entries).
 * @return dir sector number or -1 if not found.
 */
int find_dir(struct table_directory *t_dir, char *s_path, struct file_dir_entry *cur_entries){
	int exists = 0;
	int i;
	int s_dir = 0;
	printf("- Searching path: %s \n", s_path);
	const char delimiter[2] = "/";
	char *e_name = strtok(s_path, delimiter);

	// Verify if path exists and navigate through
	while( e_name != NULL ) 
	{	
		printf("- Searching dir: %s \n", e_name);
		exists = 0;

		//verify if	dir exist on current entries
		for(i = 0; i < MAX_DIR_ENTRIES; i++){
			if(strcmp(cur_entries[i].name, e_name) == 0 && cur_entries[i].dir == 1){
				exists = 1;
				s_dir = cur_entries[i].sector_start;
				ds_read_sector(cur_entries[i].sector_start, (void*)t_dir, SECTOR_SIZE);
				cur_entries = t_dir->entries;
				break;
			}
		}

		e_name = strtok(NULL, delimiter);

		if(exists == 0){
			printf("Error: The path doesn't exist\n");
			return -1;
		}
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
	char *str = malloc(sizeof(s_path));
	strcpy(str, s_path);
	
	const char delimiter[2] = "/";
	char *e_name = strtok(str, delimiter);

	int i = 0;
	int isRoot = 1;
	int length;
	struct file_dir_entry* cur_entries;
	int s_dir;
	struct table_directory t_dir;
	cur_entries = root_dir.entries;

	/* open file */
	FILE *fileptr;
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
	
	do{

		// have read all file
		if( ftell(fileptr) == filelen ){
			break;
		}

		// clear sector data
		memset(sector.data, 0, sizeof(sector.data));
		//sprintf(sector.data, "%d", SECTOR_DATA_SIZE);

		// have more than 508 bytes to read	

		// write data to sector
		data_amount = fread(sector.data, 1, SECTOR_DATA_SIZE, fileptr);

		if(data_amount == SECTOR_DATA_SIZE){
			sector.next_sector = sector_number + 1;
		} else{
			sector.next_sector = 0;
			root_dir.free_sectors_list = sector_number + 1;
			ds_write_sector(0, (void*)&root_dir, SECTOR_SIZE);
		}
		
		// move the file pointer for the data_amount available at this iteration
		ds_write_sector(sector_number, (void*)&sector, SECTOR_SIZE);
		sector_number++;
	} while(data_amount == SECTOR_DATA_SIZE);

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
	
	printf("- Copying: '%s' to '%s'\n", simul_file, output_file);
	
	/* initiate base */
	struct sector_data sector;
	struct root_table_directory root_dir;
	ds_read_sector(0, (void*)&root_dir, SECTOR_SIZE);

	/* set path */
	char *s_name = strdup(basename(simul_file));
	char *s_path = strdup(dirname(simul_file));
	char *str = malloc(sizeof(s_path));
	strcpy(str, s_path);	
	
	const char delimiter[2] = "/";
	char *e_name = strtok(str, delimiter);

	int i = 0;
	int length;
	struct file_dir_entry* cur_entries;
	int s_dir;
	struct table_directory t_dir;
	cur_entries = root_dir.entries;
	int data_amount = 0;
	int left_data;


	FILE *fileptr;

	fileptr = fopen(output_file, "w");

	// is not root, search dir
	if( e_name != NULL ) {
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
			break;
		}

		// if didnt break, all slots are in use
		if(i == length - 1){
			printf("File does not exist\n");
			return 1;
		}
	}

	left_data = cur_entries[i].size_bytes;
	ds_read_sector(cur_entries[i].sector_start, (void*)&sector, SECTOR_SIZE);

	while(left_data > 0){
		if(left_data > SECTOR_DATA_SIZE){
			data_amount = SECTOR_DATA_SIZE;
			left_data -= SECTOR_DATA_SIZE;
		} else {
			data_amount = left_data;
			left_data = 0;
		}
		
		fwrite(sector.data, sizeof(char), data_amount, fileptr);

		ds_read_sector(sector.next_sector, (void*)&sector, SECTOR_SIZE);
	}

	fclose(fileptr);
	
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
	
	printf("- Deleting: '%s' \n", simul_file);
	
	/* initiate base */
	struct sector_data sector;
	struct root_table_directory root_dir;
	ds_read_sector(0, (void*)&root_dir, SECTOR_SIZE);

	/* set path */
	char *s_name = strdup(basename(simul_file));
	char *s_path = strdup(dirname(simul_file));
	char *str = malloc(sizeof(s_path));
	strcpy(str, s_path);	
	
	const char delimiter[2] = "/";
	char *e_name = strtok(str, delimiter);

	int i = 0;
	int length;
	struct file_dir_entry* cur_entries;
	int s_dir;
	struct table_directory t_dir;
	cur_entries = root_dir.entries;
	int isRoot = 1;
	int sector_number;

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
			break;
		}

		// if didnt break, all slots are in use
		if(i == length - 1){
			printf("File does not exist\n");
			return 1;
		}
	}

	ds_read_sector(cur_entries[i].sector_start, (void*)&sector, SECTOR_SIZE);
	while(sector.next_sector != 0){
		sector_number = sector.next_sector;
		ds_read_sector(sector.next_sector, (void*)&sector, SECTOR_SIZE);
	}

	// sectors added to the beggining of free_sectors_list
	sector.next_sector = root_dir.free_sectors_list;
	root_dir.free_sectors_list = cur_entries[i].sector_start;
	
	// cleaned entry
	cur_entries[i].dir = 0;
	memset(cur_entries[i].name, 0, strlen(cur_entries[i].name));
	cur_entries[i].size_bytes = 0;
	cur_entries[i].sector_start = 0;	

	if(isRoot == 0){
		ds_write_sector(s_dir, (void*)&t_dir, SECTOR_SIZE);
	}
	ds_write_sector(sector_number, (void*)&sector, SECTOR_SIZE);
	ds_write_sector(0, (void*)&root_dir, SECTOR_SIZE);
	printf("Deleted successfully\n");
	
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
	
	/* initiate base */
	struct root_table_directory root_dir;
	ds_read_sector(0, (void*)&root_dir, SECTOR_SIZE);

	/* set path */
	char *s_path = dir_path;
	char *str = malloc(sizeof(s_path));
	strcpy(str, s_path);
	
	const char delimiter[2] = "/";
	char *e_name = strtok(str, delimiter);

	int i = 0;
	int length;
	struct file_dir_entry* cur_entries;
	int s_dir;
	struct table_directory t_dir;
	cur_entries = root_dir.entries;
	int count = 0;

		// is not root, search dir
	if( e_name != NULL ) {
		if((s_dir = find_dir(&t_dir, s_path, cur_entries)) < 1){
			return 1;
		}
		length = MAX_DIR_ENTRIES;
		cur_entries = t_dir.entries;
	}else{
		length = MAX_ROOT_ENTRIES;
		cur_entries = root_dir.entries;
	}

	printf("- Listing entries at: '%s'\n", dir_path);
	for(i=0; i < length; i++){
		// Verify if is file or dir
		if(cur_entries[i].dir == 0){
			// dir = 0 and size_byte = 0, means unused entry
			if(cur_entries[i].size_bytes == 0){
				break;
			}
			printf("f ");
		}else {
			printf("d ");
		}

		printf("%s", cur_entries[i].name);
		if(cur_entries[i].size_bytes > 0){
			printf("\t%d bytes", cur_entries[i].size_bytes);
		}
		printf("\n");
		count++;
	}

	if(count == 0){
		printf("This directory is empty\n");
	}else if(count == 1){
		printf("%d entry found\n", count);
	} else{
		printf("%d entries found\n", count);
	}
	
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
	
	printf("- Creating directory: '%s' \n", directory_path);
	
	/* initiate base */
	struct table_directory table_dir;
	struct root_table_directory root_dir;
	ds_read_sector(0, (void*)&root_dir, SECTOR_SIZE);

	/* set path */
	char *s_name = strdup(basename(directory_path));
	char *s_path = strdup(dirname(directory_path));
	char *str = malloc(sizeof(s_path));
	strcpy(str, s_path);
	
	const char delimiter[2] = "/";
	char *e_name = strtok(str, delimiter);

	int i = 0;
	int length;
	struct file_dir_entry* cur_entries;
	int s_dir;
	struct table_directory t_dir;
	cur_entries = root_dir.entries;
	int isRoot = 1;
	int sector_number;


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

	for(i=0; i < length; i++){
		if(strcmp(cur_entries[i].name, s_name) == 0 && cur_entries[i].dir == 1){
			printf("Directory already exists\n");
			return 1;
		}

		if(cur_entries[i].sector_start == 0){
			break;
		}

		// if didnt break, all slots are in use
		if(i == length - 1){
			printf("File does not exist\n");
			return 1;
		}
	}

	sector_number = root_dir.free_sectors_list;
	root_dir.free_sectors_list++;

	// set entry dir
	cur_entries[i].dir = 1;
	strcpy(cur_entries[i].name, s_name);
	cur_entries[i].sector_start = sector_number;
	cur_entries[i].size_bytes = 0;

	// write dir
	memset(&table_dir, 0, sizeof(table_dir));	
	ds_write_sector(sector_number, (void*)&table_dir, SECTOR_SIZE);

	// dir owner
	if(isRoot == 1){
		root_dir.entries[i] = cur_entries[i];
	}else{
		t_dir.entries[i] = cur_entries[i];
		ds_write_sector(s_dir, (void*)&t_dir, SECTOR_SIZE);
	}
	
	ds_write_sector(0, (void*)&root_dir, SECTOR_SIZE);

	printf("Directory created successfully\n");
	
	ds_stop();
	
	return 0;
}

/**
 * @brief Remove directory from the simulated filesystem.
 * @param directory_path directory path.
 * @return 0 on success.
 */
int fs_rmdir(char *dir_path){
	int ret;
	if ( (ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0 ){
		return ret;
	}
	
	/* initiate base */
	struct root_table_directory root_dir;
	ds_read_sector(0, (void*)&root_dir, SECTOR_SIZE);

	/* set path */
	char *s_name = basename(dir_path);
	char *s_path = dirname(dir_path);
	char *str = malloc(sizeof(s_path));
	strcpy(str, s_path);
	
	const char delimiter[2] = "/";
	char *e_name = strtok(str, delimiter);

	int i, j = 0;
	struct file_dir_entry* cur_entries;
	int s_dir;
	struct table_directory t_dir;
	struct table_directory delete_dir;
	cur_entries = root_dir.entries;
	int sector_number;
	int is_empty = 1;

	// is not root, search dir
	if( e_name != NULL ) {
		if((s_dir = find_dir(&t_dir, s_path, cur_entries)) < 1){
			return 1;
		}
		cur_entries = t_dir.entries;
	}else{
		printf("ERROR: You cannot remove root dir.\n");
		return 1;
	}

	for(i=0; i < MAX_DIR_ENTRIES; i++){
		// Verify if is file or dir
		if(strcmp(cur_entries[i].name, s_name)== 0){
			sector_number = cur_entries[i].sector_start;
			break;
		}

		if(i == MAX_DIR_ENTRIES-1){
			printf("Error: The path doesn't exist\n");
			return 1;
		}
	}

	ds_read_sector(sector_number, (void*)&delete_dir, SECTOR_SIZE);

	for(j=0; j < MAX_DIR_ENTRIES; j++){
		if(delete_dir.entries[j].sector_start != 0){
			is_empty = 0;
		}
	}

	if(is_empty == 1){
		// remove reference from parent dir
		
		t_dir.entries[i].dir = 0;
		memset(t_dir.entries[i].name, 0, strlen(t_dir.entries[i].name));
		t_dir.entries[i].size_bytes = 0;
		t_dir.entries[i].sector_start = 0;

		ds_write_sector(s_dir, (void*)&t_dir, SECTOR_SIZE);
		printf("Directory was successfully removed\n");
	}else{
		printf("Error: Directory is not empty\n");
	}
	
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


