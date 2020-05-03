///////////////////////////////////////
/// 640x480 version!
/// test VGA with hardware video input copy to VGA
// compile with
// gcc pio_test_2.c -o pio2 
///////////////////////////////////////
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ipc.h> 
#include <sys/shm.h> 
#include <sys/mman.h>
#include <sys/time.h> 
#include <math.h> 
#include <time.h>

// main bus; PIO
#define FPGA_AXI_BASE 	0xC0000000
#define FPGA_AXI_SPAN   0x00001000
// main axi bus base
void *h2p_virtual_base;
volatile unsigned int * axi_pio_ptr = NULL ;
volatile unsigned int * axi_pio_read_ptr = NULL ;

// lw bus; PIO
#define FPGA_LW_BASE 	0xff200000
#define FPGA_LW_SPAN	0x00001000
// the light weight bus base
void *h2p_lw_virtual_base;
// HPS_to_FPGA FIFO status address = 0
volatile unsigned int * lw_pio_ptr = NULL ;
volatile unsigned int * lw_pio_read_ptr = NULL ;

// read offset is 0x10 for both busses
// remember that eaxh axi master bus needs unique address
#define FPGA_PIO_READ	0x10
#define FPGA_PIO_WRITE	0x00


// /dev/mem file id
int fd;	

// timing vars
// timer variables
struct timeval t1, t2;
double elapsedTime;

struct matrix {
	int cols;
	int rows;
	float ** matrix;
};

float sigmoid(float x) {
     float exp_value;
     float return_value;

     /*** Exponential calculation ***/
     exp_value = exp((float) -x);

     /*** Final sigmoid value ***/
     return_value = 1 / (1 + exp_value);

     return return_value;
}

int * results(struct matrix mat) {
	int i, j;
	
	int * temp = (int *)malloc(mat.rows * sizeof(int));
	
    int max = 0;
    float maxVal = 0;
    	
    for(i = 0; i < mat.rows; i++) {
    	for(j = 0; j < mat.cols; j++) {
    		if(mat.matrix[i][j] > maxVal) {
    			maxVal = mat.matrix[i][j];
    			max = j;
    		} 
    	}
    	temp[i]= max;
    	maxVal = 0;
    	max = 0;
    }
	
	
	return temp;
}

struct matrix activation(struct matrix mat) {
	int i, j;
	
	struct matrix tempStruct;
	
	float ** temp = (float **)malloc(mat.rows * sizeof(double*));
	for(i = 0; i < mat.rows; i++) {
		temp[i] = (float *)malloc(mat.cols * sizeof(double));
	}
									   
	
	for(i = 0; i < mat.rows; i++) {
		for(j = 0; j < mat.cols; j++) {
			temp[i][j] = sigmoid(mat.matrix[i][j]);
		}
	}
	
	tempStruct.rows = mat.rows;
	tempStruct.cols = mat.cols;
	tempStruct.matrix = temp;
	
	return tempStruct;
}

struct matrix matrixMultiply(struct matrix mat1, struct matrix mat2) {
	if(mat1.cols != mat2.rows) {
		printf("Matrix dimensions do not match.\n");
		exit(0);
	}
	
	int i, j, k;
	
	struct matrix tempStruct;
	
	float ** temp = (float **)malloc(mat1.rows*sizeof(double*)); 
	for(i = 0; i < mat1.rows; ++i)
		temp[i] = (float *)malloc(mat2.cols * sizeof(double));
	
	double sum = 0;
	
	for(i = 0; i < mat1.rows; i++) {
		for(j = 0; j < mat2.cols; j++) {
			for(k = 0; k < mat2.rows; k++) {
				sum = sum + mat1.matrix[i][k] * mat2.matrix[k][j];
			}
			
			temp[i][j] = sum;
			sum = 0;
		}
	}
	
	tempStruct.rows = mat1.rows;
	tempStruct.cols = mat2.cols;
	tempStruct.matrix = temp;
	
	return tempStruct;
}

struct matrix getWeights(char filename[50], int rows, int cols) {
	int i,j;

	struct matrix temp;

	float ** mat = (float **)malloc(rows*sizeof(float *)); 
	for(i = 0; i < rows; ++i)
		mat[i] = (float *)malloc(cols * sizeof(float));


  	FILE *file;
  	file = fopen(filename, "r");

 	for(i = 0; i < rows; i++) {
    	for(j = 0; j < cols; j++) {
       		if (!fscanf(file, "%f", &mat[i][j])) 
           		break;
      		 
       		//printf("%lf, ",mat[i][j]); //Use lf format specifier, \n is for new line
   		}
		
		//printf("\n");
	}
	
  	fclose(file);
	
	temp.matrix = mat;
	temp.rows = rows;
	temp.cols = cols;
	
	return temp;
}

void hpstofpga(int addr, int reg, int data) {
    *(lw_pio_ptr) = ((addr) | (reg << 20) | (1 << 30));
    *(axi_pio_ptr) = data;
}

int fpgatohps(int addr, int reg) {
    *(lw_pio_ptr) = (addr | (reg << 20));
	while(*(axi_pio_read_ptr) == 0) {}
    return *(axi_pio_read_ptr) ;
}
	
int main(void)
{

	// Declare volatile pointers to I/O registers (volatile 	
	// means that IO load and store instructions will be used 	
	// to access these pointer locations,  
  
	// === get FPGA addresses ==================
    // Open /dev/mem
	if( ( fd = open( "/dev/mem", ( O_RDWR | O_SYNC ) ) ) == -1 ) 	{
		printf( "ERROR: could not open \"/dev/mem\"...\n" );
		return( 1 );
	}
    
	//============================================
    // get virtual addr that maps to physical
	// for light weight AXI bus
	h2p_lw_virtual_base = mmap( NULL, FPGA_LW_SPAN, ( PROT_READ | PROT_WRITE ), MAP_SHARED, fd, FPGA_LW_BASE );	
	if( h2p_lw_virtual_base == MAP_FAILED ) {
		printf( "ERROR: mmap1() failed...\n" );
		close( fd );
		return(1);
	}
	// Get the addresses that map to the two parallel ports on the light-weight bus
	lw_pio_ptr = (unsigned int *)(h2p_lw_virtual_base);
	lw_pio_read_ptr = (unsigned int *)(h2p_lw_virtual_base + FPGA_PIO_READ);
	
	//============================================
	
	// ===========================================
	// get virtual address for
	// AXI bus addr 
	h2p_virtual_base = mmap( NULL, FPGA_AXI_SPAN, ( PROT_READ | PROT_WRITE ), MAP_SHARED, fd, FPGA_AXI_BASE); 	
	if( h2p_virtual_base == MAP_FAILED ) {
		printf( "ERROR: mmap3() failed...\n" );
		close( fd );
		return(1);
	}
    // Get the addresses that map to the two parallel ports on the AXI bus
	axi_pio_ptr =(unsigned int *)(h2p_virtual_base);
	axi_pio_read_ptr =(unsigned int *)(h2p_virtual_base + FPGA_PIO_READ);
	//============================================
	
	struct matrix hiddenWeights;
	struct matrix testValues;
	struct matrix outputWeights;
	struct matrix testLabels;
	outputWeights = getWeights("outputWeights.txt", 40, 10);
	hiddenWeights = getWeights("hiddenWeights.txt", 784, 40);
	testValues = getWeights("testValues.txt", 10000, 784);
	testLabels = getWeights("testLabels.txt", 10000, 1);
	
	int i;
	int wait;
	int j;

	clock_t start, end;
	double runtime;
	
	start = clock();
	
	struct matrix tV = testValues;
	struct matrix hW = hiddenWeights;
	struct matrix oW = outputWeights;
	
	struct matrix result = activation(matrixMultiply(activation(matrixMultiply(tV, hW)),oW));
	
	
	int * r = results(result);
	
	int correct = 0;
	
	for(i = 0; i < 10000; i++) {
		if(testLabels.matrix[i][0] == r[i])
			correct++;
	}
	
	printf("Correct: %d\n", correct);
	
	end = clock();
	runtime = ((double) (end-start)) / CLOCKS_PER_SEC;
	printf("Run Time: %lf\n", runtime);
	
	int N = 40;
	int T = 1;
	float multiplier = 4096.0;
	float total;
	int temp;
	float max = 0;
	float output[40];
	int readyHidden;
	int readyOut;
	
	for(j = 0; j < N; j++) {
		for(i = 0; i < 784; i++) {
			temp = (int) (hiddenWeights.matrix[i][j] * multiplier);
			hpstofpga(i, j+1, temp);
		}
	}
	
	for(j = 0; j < 10; j++) {
		for(i = 0; i < 40; i++) {
			temp = (int) (outputWeights.matrix[i][j] * multiplier);
			hpstofpga(i, j+41, temp);
		}
	}
	
	/*
	for(i = 0; i < 784; i++) {
		double result = fpgatohps(i,0) / multiplier;
		printf("input: %lf\n", result);
	}
	
	for(i = 0; i < 40; i++) {
		float result = fpgatohps(i,41) / multiplier;
		printf("output: %f\n", result);
	}
	*/
	
	correct = 0;
	start = clock();
	
	for(T = 0; T < 10000; T++) {
		//printf("Enter index: ");
		//scanf("%d", &T);
		/*
		for(i = 0; i < N; i++) {
			total = 0;
			for(j = 0; j < 784; j++) {
				total = testValues.matrix[T][j] * hiddenWeights.matrix[j][i] + total;
			}
			total = sigmoid(total);
			printf("total %d: %lf \n", i, total);
		}
		*/
		for(i = 0; i < 784; i++) {
			temp = (int) (testValues.matrix[T][i] * multiplier);
			hpstofpga(i, 0, temp);
		}

		//sleep(2);
		while(readyHidden == 0) {
			readyHidden = fpgatohps(0, 51);
		}

		for(i = 0; i < N; i++) {
			int result = fpgatohps(i,1000);
			float res = (float)(result / (multiplier * multiplier));
			//printf("result %d: %f\n", i, res);
			output[i] = sigmoid(res);
			//printf("Hidden %d: %f\n", i, output[i]);
		}
		
		for(i = 0; i < N; i++) {
			hpstofpga(i,100,(output[i]*multiplier));
			//int temp = 1 * multiplier;
			//hpstofpga(i,100,temp);
		}
		
		//sleep(2);
		readyOut = fpgatohps(0,52);
		while(readyOut == 0) {
			readyOut = fpgatohps(0, 52);
		}
		
		float max = 0;
		int number = 0;
		for(i = 0; i < 10; i++) {
			int result = fpgatohps(i,1001);
			float res = (float)(result / (multiplier * multiplier));
			res = sigmoid(res);
			//printf("Output %d: %f\n", i, res);
			
			if(res > max) {
				max = res;
				number = i;
			}
		}
		
		if(number == testLabels.matrix[T][0])
			correct++;
		
		//printf("Number is: %d\n", number);
		//printf("Correct number is: %d\n", (int) testLabels.matrix[T][0]);
	}
	
	printf("Correct: %d \n",correct);
	
	end = clock();
	runtime = ((double) (end-start)) / CLOCKS_PER_SEC;
	printf("Run Time: %lf\n", runtime);

	free(result.matrix);
	free(r);
	free(outputWeights.matrix);
	free(hiddenWeights.matrix);
	free(testValues.matrix);
	free(testLabels.matrix);
	
    return 0;
} // end main


/// /// ///////////////////////////////////// 
/// end /////////////////////////////////////
