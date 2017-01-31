//sunhm: All *.cpp files must invoke the following line, or error
//  occurs when executing compiler with precompile options.
#include "stdafx.h"

#include "common image functions.h"

//Defined for getting the gradient image.
#define GRADIENT_THRESHOLD 18*15  //Reasonable values are 18*gray-level(between 0 and 255).

//Defined for extracting the bounding rectangles of connected components.
#define LABEL_NUM_BOUND_FOR_IMAGE        150000  //Must be less than 2^20.
#define MAX_NUM_OF_CHAR_LIKE_BLOCK         1000  //Must be less than 5957.

//Defined for checking the bounding rectangle of every connected components.
#define CHARACTER_HEIGHT_UPPER_BOUND 1200  //Pixels.
#define CHARACTER_WIDTH_UPPER_BOUND  1200  //Pixels.
#define NOISE_SIZE                     25  //Pixels.

//Defined for classifying the blocks based on their colors.
#define MAX_NUM_OF_BLOCK_CLASS           50
#define COLOR_DIFF_THRESHOLD             120

//Defined for grouping the blocks by recursive XY-cut.
#define MAX_REGION_NUM                   100
#define MAX_MEMBER_NUM                   500

//Defined for checking if a color is close to the background color.
#define COLOR_DIFFUSION               90

//Defined for identifying the text blocks.
#define TEXT_REGION_WIDTH_MIN			0.5/2.54	//Evaluated by inch.
#define TEXT_REGION_HEIGHT_MIN			0.2/2.54	//Evaluated by inch.
#define TEXT_REGION_HEIGHT_MAX			3.0/2.54	//Evaluated by inch.
#define TEXT_REGION_ASPECT_RATIO_MIN	1.7  //Old value: 2.1

#define PARAMETER2                    13
#define CHAR_ASPECT                   1.6  //Old value: 1.35
#define BLOCK_SATURATION              0.4

//Defined for verification of large and small text blocks.
#define SMALL_TEXT_HEIGHT_MAX			1.0/2.54	//Evaluated by inch.
#define SMALL_TEXT_BLOCK_RUNLENGTH_MAX	0.41/2.54	//Evaluated by inch.
#define LARGE_TEXT_BLOCK_RUNLENGTH_MIN	0.058/2.54	//Evaluated by inch.
#define LARGE_TEXT_BLOCK_RUNLENGTH_MAX	1.14/2.54	//Evaluated by inch.
#define NOISE_RUN_LENGTH				0.025/2.54	//Evaluated by inch.
#define SMALL_TEXT_BLOCK_ASPECT_MIN		1.5		//Minimun of Width/Height of small text blocks.
#define LARGE_TEXT_BLOCK_ASPECT_MIN		1.5		//Minimun of Width/Height of large text blocks (Old value is 1.83).
#define SMALL_TEXT_BLOCK_TRANSITION_COUNT_MIN	1.00
#define SMALL_TEXT_BLOCK_TRANSITION_COUNT_MAX	3.80
#define LARGE_TEXT_BLOCK_TRANSITION_COUNT_MIN	1.14
#define LARGE_TEXT_BLOCK_TRANSITION_COUNT_MAX	6.00
#define NOISE_RUNLENGTH_IN_SMALL_TEXT_BLOCK		0.013/2.54	//Evaluated by inch.
#define MAX_BLOCK_WIDTH							5000		//In pixels.

struct REGION  //Structure for recording the regions in the projection.
{
	U_WORD uwLow, uwHigh;
};

struct NEWBLOCK
{
	U_WORD uwMinRow, uwMaxRow, uwMinEntry, uwMaxEntry;
	U_BYTE ubMeanOfRColorValue, ubMeanOfGColorValue, ubMeanOfBColorValue;
};

int iNoiseRunLengthInSmallText;  //Set in ExtractTextFromGraphics(), used
										//  in SmallTextBlockVerification2().

//The following global parameters are set in ExtractTextFromGraphics() and
//  used in ExtractTextFromGraphics(), HProjectAndCut() and VProjectAndCut().
U_WORD uwMaxSmallText,
	   uwTextRegionHeightMin, uwTextRegionHeightMax, uwTextRegionWidthMin;
float fSmallTextRunLengthMax,
	  fLargeTextRunLengthMin, fLargeTextRunLengthMax;
int iNoiseRunLength;

int ExtractTextFromGraphics(IMAGEDATA &Image)
{
   //Show waiting cursor.
   HCURSOR hCursor=SetCursor(LoadCursor(NULL, IDC_WAIT));

   //For debug.
   time_t StartTime=time(NULL);  //Get the current time in integer formats.
   clock_t StartClock=clock();   //Get the current time in float formats.

   //Allocate a new memory object to store the gradient image.
   U_WORD uwImageWidth=Image.iWidth;
   U_WORD uwImageHeight=Image.iHeight;
   U_WORD uwNewBytesPerRow= uwImageWidth%8 ? uwImageWidth/8+1 : uwImageWidth/8;
   if(uwNewBytesPerRow%2)
      uwNewBytesPerRow++;  //BytesPerRow must be an even number.
   S_DWORD sdwNewImageVolume=S_DWORD(uwNewBytesPerRow)*\
                             S_DWORD(uwImageHeight);
   U_WORD uwNewRowVolume=uwNewBytesPerRow;

   HGLOBAL hglbNewImage=GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,\
                                    (DWORD)sdwNewImageVolume);
   if(!hglbNewImage)
      //Means memory allocation error.
      return 1;

   PBMPIMAGE pNewBmpImage=(PBMPIMAGE)GlobalLock(hglbNewImage);
   if(!pNewBmpImage)
   {
      //Means memory locking error.
      GlobalFree(hglbNewImage);
      return 2;
   }

   PBMPIMAGE pOriBmpImage=(PBMPIMAGE)GlobalLock(Image.hglbImage);
   if(!pOriBmpImage)
   {
      //Means memory locking error.
      GlobalUnlock(hglbNewImage);
      GlobalFree(hglbNewImage);
      return 3;
   }

   //Now, begin to calculate the gradient of the original image and--
   //--write the result to the new image.
   //We don't calculate the gradient of the top, bottom, left edge, and--
   //--right edge pixels of the original image, so we just write 0 to these--
   //--position in the new image.
/* We have set the allocated memory to have 0 as initial value, so need not do this.
   for(S_DWORD sdwIndex=0; sdwIndex<uwNewBytesPerRow; sdwIndex++)
      pNewBmpImage[sdwIndex]=0;
*/
/*
   //For debug.
   static S_DWORD sdwGradientAccumulator[4591];
   for(i=0; i<4591; i++)
      sdwGradientAccumulator[i]=0;
*/
   //For debug.
   S_WORD swMaxGradientMag=0;

   U_WORD uwHeightBound=uwImageHeight-1;
   U_WORD uwWidthBound=uwImageWidth-1;
   S_DWORD sdwOriImageVolume=S_DWORD(Image.iBytesPerRow)*\
                             S_DWORD(uwImageHeight)*S_DWORD(3);
   U_WORD uwOriRowVolume=Image.iBytesPerRow*U_WORD(3);

   U_WORD uwXOffset1=3, uwXOffset2=6, uwXOffset3=2*uwOriRowVolume,\
          uwXOffset4=uwXOffset3+3, uwXOffset5=uwXOffset3+6,\
          uwYOffset1=uwXOffset5, uwYOffset2=uwOriRowVolume+6, uwYOffset3=6,\
          uwYOffset4=uwXOffset3, uwYOffset5=uwOriRowVolume;
   S_DWORD sdwBase, sdwIndex;
   U_WORD i;
   for(i=1, sdwBase=sdwOriImageVolume-S_DWORD(uwOriRowVolume)*S_DWORD(3);\
       i<uwHeightBound; i++, sdwBase-=uwOriRowVolume)
   {
	  sdwIndex=sdwBase+2;
      for(U_WORD j=1; j<uwWidthBound; j++, sdwIndex+=5)
      {
         S_WORD swGradientR_X, swGradientR_Y,\
                swGradientG_X, swGradientG_Y,\
                swGradientB_X, swGradientB_Y;
/*
         swGradientR_X=GetR(pOriBmpImage, sdwImageVolume, uwRowVolume, i+1, j-1)+\
                       2*GetR(pOriBmpImage, sdwImageVolume, uwRowVolume, i+1, j)+\
                       GetR(pOriBmpImage, sdwImageVolume, uwRowVolume, i+1, j+1)-\
                       GetR(pOriBmpImage, sdwImageVolume, uwRowVolume, i-1, j-1)-\
                       2*GetR(pOriBmpImage, sdwImageVolume, uwRowVolume, i-1, j)-\
                       GetR(pOriBmpImage, sdwImageVolume, uwRowVolume, i-1, j+1);

         swGradientR_Y=GetR(pOriBmpImage, sdwImageVolume, uwRowVolume, i-1, j+1)+\
                       2*GetR(pOriBmpImage, sdwImageVolume, uwRowVolume, i, j+1)+\
                       GetR(pOriBmpImage, sdwImageVolume, uwRowVolume, i+1, j+1)-\
                       GetR(pOriBmpImage, sdwImageVolume, uwRowVolume, i-1, j-1)-\
                       2*GetR(pOriBmpImage, sdwImageVolume, uwRowVolume, i, j-1)-\
                       GetR(pOriBmpImage, sdwImageVolume, uwRowVolume, i+1, j-1);
*/
         swGradientR_X=pOriBmpImage[sdwIndex]+2*pOriBmpImage[sdwIndex+uwXOffset1]+\
                       pOriBmpImage[sdwIndex+uwXOffset2]-pOriBmpImage[sdwIndex+uwXOffset3]-
                       2*pOriBmpImage[sdwIndex+uwXOffset4]-pOriBmpImage[sdwIndex+uwXOffset5];

         swGradientR_Y=pOriBmpImage[sdwIndex+uwYOffset1]+2*pOriBmpImage[sdwIndex+uwYOffset2]+\
                       pOriBmpImage[sdwIndex+uwYOffset3]-pOriBmpImage[sdwIndex+uwYOffset4]-
                       2*pOriBmpImage[sdwIndex+uwYOffset5]-pOriBmpImage[sdwIndex];

         //Get the absolute value for computing the color difference later.
         if(swGradientR_X<0) swGradientR_X*=-1;
         if(swGradientR_Y<0) swGradientR_Y*=-1;
/*
         swGradientG_X=GetG(pOriBmpImage, sdwImageVolume, uwRowVolume, i+1, j-1)+\
                       2*GetG(pOriBmpImage, sdwImageVolume, uwRowVolume, i+1, j)+\
                       GetG(pOriBmpImage, sdwImageVolume, uwRowVolume, i+1, j+1)-\
                       GetG(pOriBmpImage, sdwImageVolume, uwRowVolume, i-1, j-1)-\
                       2*GetG(pOriBmpImage, sdwImageVolume, uwRowVolume, i-1, j)-\
                       GetG(pOriBmpImage, sdwImageVolume, uwRowVolume, i-1, j+1);

         swGradientG_Y=GetG(pOriBmpImage, sdwImageVolume, uwRowVolume, i-1, j+1)+\
                       2*GetG(pOriBmpImage, sdwImageVolume, uwRowVolume, i, j+1)+\
                       GetG(pOriBmpImage, sdwImageVolume, uwRowVolume, i+1, j+1)-\
                       GetG(pOriBmpImage, sdwImageVolume, uwRowVolume, i-1, j-1)-\
                       2*GetG(pOriBmpImage, sdwImageVolume, uwRowVolume, i, j-1)-\
                       GetG(pOriBmpImage, sdwImageVolume, uwRowVolume, i+1, j-1);
*/
         sdwIndex--;

         swGradientG_X=pOriBmpImage[sdwIndex]+2*pOriBmpImage[sdwIndex+uwXOffset1]+\
                       pOriBmpImage[sdwIndex+uwXOffset2]-pOriBmpImage[sdwIndex+uwXOffset3]-
                       2*pOriBmpImage[sdwIndex+uwXOffset4]-pOriBmpImage[sdwIndex+uwXOffset5];

         swGradientG_Y=pOriBmpImage[sdwIndex+uwYOffset1]+2*pOriBmpImage[sdwIndex+uwYOffset2]+\
                       pOriBmpImage[sdwIndex+uwYOffset3]-pOriBmpImage[sdwIndex+uwYOffset4]-
                       2*pOriBmpImage[sdwIndex+uwYOffset5]-pOriBmpImage[sdwIndex];

         //Get the absolute value for computing the color difference later.
         if(swGradientG_X<0) swGradientG_X*=-1;
         if(swGradientG_Y<0) swGradientG_Y*=-1;
/*
         swGradientB_X=GetB(pOriBmpImage, sdwImageVolume, uwRowVolume, i+1, j-1)+\
                       2*GetB(pOriBmpImage, sdwImageVolume, uwRowVolume, i+1, j)+\
                       GetB(pOriBmpImage, sdwImageVolume, uwRowVolume, i+1, j+1)-\
                       GetB(pOriBmpImage, sdwImageVolume, uwRowVolume, i-1, j-1)-\
                       2*GetB(pOriBmpImage, sdwImageVolume, uwRowVolume, i-1, j)-\
                       GetB(pOriBmpImage, sdwImageVolume, uwRowVolume, i-1, j+1);

         swGradientB_Y=GetB(pOriBmpImage, sdwImageVolume, uwRowVolume, i-1, j+1)+\
                       2*GetB(pOriBmpImage, sdwImageVolume, uwRowVolume, i, j+1)+\
                       GetB(pOriBmpImage, sdwImageVolume, uwRowVolume, i+1, j+1)-\
                       GetB(pOriBmpImage, sdwImageVolume, uwRowVolume, i-1, j-1)-\
                       2*GetB(pOriBmpImage, sdwImageVolume, uwRowVolume, i, j-1)-\
                       GetB(pOriBmpImage, sdwImageVolume, uwRowVolume, i+1, j-1);
*/
         sdwIndex--;

         swGradientB_X=pOriBmpImage[sdwIndex]+2*pOriBmpImage[sdwIndex+uwXOffset1]+\
                       pOriBmpImage[sdwIndex+uwXOffset2]-pOriBmpImage[sdwIndex+uwXOffset3]-
                       2*pOriBmpImage[sdwIndex+uwXOffset4]-pOriBmpImage[sdwIndex+uwXOffset5];

         swGradientB_Y=pOriBmpImage[sdwIndex+uwYOffset1]+2*pOriBmpImage[sdwIndex+uwYOffset2]+\
                       pOriBmpImage[sdwIndex+uwYOffset3]-pOriBmpImage[sdwIndex+uwYOffset4]-
                       2*pOriBmpImage[sdwIndex+uwYOffset5]-pOriBmpImage[sdwIndex];

         //Get the absolute value for computing the color difference later.
         if(swGradientB_X<0) swGradientB_X*=-1;
         if(swGradientB_Y<0) swGradientB_Y*=-1;

         S_WORD swGradientMag=swGradientR_X+swGradientG_X+swGradientB_X+\
                              swGradientR_Y+swGradientG_Y+swGradientB_Y;

         //For debug.
//			sdwGradientAccumulator[swGradientMag]++;
         if(swGradientMag>swMaxGradientMag) swMaxGradientMag=swGradientMag;

         //Write the gradient magnitude to the new image.
         if(swGradientMag>GRADIENT_THRESHOLD)
            //Set this pixel as 1.
            SetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, i, j);
      }
   }

   //Define the BLOCK structure and allocate a block array to store the data.
   struct BLOCK
   {
      U_WORD uwMinRow, uwMinEntry, uwMaxEntry;
      U_DWORD uwMaxRow;  //The equivalent lable is also stored in this variable.
      U_BYTE ubRColorValueLow, ubRColorValueHigh,\
             ubGColorValueLow, ubGColorValueHigh,\
             ubBColorValueLow, ubBColorValueHigh;
   };

   HGLOBAL hglbBlockArray=GlobalAlloc(GMEM_MOVEABLE,\
                      DWORD(sizeof(BLOCK))*DWORD(LABEL_NUM_BOUND_FOR_IMAGE+1));
   BLOCK* Block=(BLOCK*)GlobalLock(hglbBlockArray);

   U_DWORD udwBlockIndex=1;  //Use Block[] from the second entry.

   //Allocate two buffers to record the label values.
   HGLOBAL hglbBuffer1=GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,\
                                          DWORD((uwWidthBound+1)*4));
   HGLOBAL hglbBuffer2=GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,\
                                          DWORD((uwWidthBound+1)*4));
   U_DWORD FAR* pudwLastLabeledRow=(U_DWORD FAR*)GlobalLock(hglbBuffer1);
   U_DWORD FAR* pudwCurrentLabelingRow=(U_DWORD FAR*)GlobalLock(hglbBuffer2);

   //Start connected component labelling.
   //The connected components have value 0's.
   //Note that we must start labelling from i=1, instead of i=0,--
   //--to prevent an useful label from having uwMinRow=0, because--
   //--a label with uwMinRow=0 is unuseful.
   for(i=1; i<uwHeightBound; i++)
   {
      for(U_WORD j=1; j<uwWidthBound; j++)
         if(GetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, i, j))
            pudwCurrentLabelingRow[j]=0;
         else
         {
            U_DWORD udwLabel;
            char cNewBlockFlag=1;
            U_WORD uwTemp1=j-1, uwTemp2=j+1;

            if(pudwCurrentLabelingRow[uwTemp1])
            {
               //Trace the equivalent label of this one.
               //If Block[XLabel].uwMinRow==0, then Block[XLabel].uwMaxRow is--
               //--the equivalent label of XLabel.
               for(U_DWORD k=pudwCurrentLabelingRow[uwTemp1];\
                   Block[k].uwMinRow==0; k=Block[k].uwMaxRow);

               udwLabel=k;
               cNewBlockFlag=0;
            }

            //If the up-left neighboring pixel has been labeled,--
            //--we must further check cNewBlockFlag.
            //If cNewBlockFlag==0, we need not further find out its--
            //--equivalent label, because it is just the--
            //--same as the one found above.
            if(pudwLastLabeledRow[uwTemp1] && cNewBlockFlag)
            {
               for(U_DWORD k=pudwLastLabeledRow[uwTemp1];\
                  Block[k].uwMinRow==0; k=Block[k].uwMaxRow);

               udwLabel=k;
               cNewBlockFlag=0;
            }

            //If the up neighboring pixel has been labeled,--
            //--we must further check cNewBlockFlag.
            //If cNewBlockFlag==0, we need not further find out its--
            //--equivalent label, because it is just the--
            //--same as the one found above.
            if(pudwLastLabeledRow[j] && cNewBlockFlag)
            {
               for(U_DWORD k=pudwLastLabeledRow[j];\
                  Block[k].uwMinRow==0; k=Block[k].uwMaxRow);

               udwLabel=k;
               cNewBlockFlag=0;
            }

            if(pudwLastLabeledRow[uwTemp2])
            {
               //Trace the equivalent label of this one.
               //If Block[XLabel].uwMinRow==0, then Block[XLabel].uwMaxRow is--
               //--the equivalent label of XLabel.
               for(U_DWORD k=pudwLastLabeledRow[uwTemp2];\
                   Block[k].uwMinRow==0; k=Block[k].uwMaxRow);

               if(cNewBlockFlag)
               {
                  udwLabel=k;
                  cNewBlockFlag=0;
               }
               else
                  if(k!=udwLabel)
                  {
                     BLOCK* pTemp1=Block+udwLabel;
                     BLOCK* pTemp2=Block+k;

                     //Means udwLabel and k are two equivalent labels, so update--
                     //--the data of uwLabel and set k to be equivalent to uwLabel.
                     if(pTemp1->uwMinRow>pTemp2->uwMinRow)
                        pTemp1->uwMinRow=pTemp2->uwMinRow;
                     if(pTemp1->uwMaxRow<pTemp2->uwMaxRow)
                        pTemp1->uwMaxRow=pTemp2->uwMaxRow;
                     if(pTemp1->uwMinEntry>pTemp2->uwMinEntry)
                        pTemp1->uwMinEntry=pTemp2->uwMinEntry;
                     if(pTemp1->uwMaxEntry<pTemp2->uwMaxEntry)
                        pTemp1->uwMaxEntry=pTemp2->uwMaxEntry;
                     if(pTemp1->ubRColorValueLow>pTemp2->ubRColorValueLow)
                        pTemp1->ubRColorValueLow=pTemp2->ubRColorValueLow;
                     if(pTemp1->ubRColorValueHigh>pTemp2->ubRColorValueHigh)
                        pTemp1->ubRColorValueHigh=pTemp2->ubRColorValueHigh;
                     if(pTemp1->ubGColorValueLow>pTemp2->ubGColorValueLow)
                        pTemp1->ubGColorValueLow=pTemp2->ubGColorValueLow;
                     if(pTemp1->ubGColorValueHigh>pTemp2->ubGColorValueHigh)
                        pTemp1->ubGColorValueHigh=pTemp2->ubGColorValueHigh;
                     if(pTemp1->ubBColorValueLow>pTemp2->ubBColorValueLow)
                        pTemp1->ubBColorValueLow=pTemp2->ubBColorValueLow;
                     if(pTemp1->ubBColorValueHigh>pTemp2->ubBColorValueHigh)
                        pTemp1->ubBColorValueHigh=pTemp2->ubBColorValueHigh;

                     //Mark this block label as unuseful and set its--
                     //--equivalent label in its uwMaxRow.
                     pTemp2->uwMinRow=0;
                     pTemp2->uwMaxRow=udwLabel;
                  }
            }

            if(cNewBlockFlag)
            {
               //Means having no neighboring pixels whose values are 0s.
               //First, check if the number of found blocks overflows.
               if(udwBlockIndex>LABEL_NUM_BOUND_FOR_IMAGE)
               {
                  AfxMessageBox("這份文件中 label 的數目已經超過預設值",
								MB_ICONSTOP);
                  i=uwHeightBound;
                  udwBlockIndex=1;
                  break;
               }
               else
               {
                  udwLabel=udwBlockIndex;
                  udwBlockIndex++;
                  Block[udwLabel].uwMinRow=i;
                  Block[udwLabel].uwMaxRow=i;
                  Block[udwLabel].uwMinEntry=Block[udwLabel].uwMaxEntry=j;
                  Block[udwLabel].ubRColorValueLow=Block[udwLabel].ubRColorValueHigh=\
                          GetR(pOriBmpImage, sdwOriImageVolume, uwOriRowVolume, i, j);
                  Block[udwLabel].ubGColorValueLow=Block[udwLabel].ubGColorValueHigh=\
                          GetG(pOriBmpImage, sdwOriImageVolume, uwOriRowVolume, i, j);
                  Block[udwLabel].ubBColorValueLow=Block[udwLabel].ubBColorValueHigh=\
                          GetB(pOriBmpImage, sdwOriImageVolume, uwOriRowVolume, i, j);
               }
            }
            else
            {
               BLOCK* pTemp=Block+udwLabel;

               if(pTemp->uwMaxRow<i)
                  pTemp->uwMaxRow=i;

               if(pTemp->uwMinEntry>j)
                  pTemp->uwMinEntry=j;
               else
                  if(pTemp->uwMaxEntry<j)
                     pTemp->uwMaxEntry=j;

               U_BYTE ubRColorValueOfThisPixel=\
                         GetR(pOriBmpImage, sdwOriImageVolume, uwOriRowVolume, i, j);
               U_BYTE ubGColorValueOfThisPixel=\
                         GetG(pOriBmpImage, sdwOriImageVolume, uwOriRowVolume, i, j);
               U_BYTE ubBColorValueOfThisPixel=\
                         GetB(pOriBmpImage, sdwOriImageVolume, uwOriRowVolume, i, j);
               if(pTemp->ubRColorValueLow>ubRColorValueOfThisPixel)
                  pTemp->ubRColorValueLow=ubRColorValueOfThisPixel;
               else
                  if(pTemp->ubRColorValueHigh<ubRColorValueOfThisPixel)
                     pTemp->ubRColorValueHigh=ubRColorValueOfThisPixel;

               if(pTemp->ubGColorValueLow>ubGColorValueOfThisPixel)
                  pTemp->ubGColorValueLow=ubGColorValueOfThisPixel;
               else
                  if(pTemp->ubGColorValueHigh<ubGColorValueOfThisPixel)
                     pTemp->ubGColorValueHigh=ubGColorValueOfThisPixel;

               if(pTemp->ubBColorValueLow>ubBColorValueOfThisPixel)
                  pTemp->ubBColorValueLow=ubBColorValueOfThisPixel;
               else
                  if(pTemp->ubBColorValueHigh<ubBColorValueOfThisPixel)
                     pTemp->ubBColorValueHigh=ubBColorValueOfThisPixel;
            }

            //Write the label to the corresponding entry of this pixel in the--
            //--pudwCurrentLabelingRow[].
            pudwCurrentLabelingRow[j]=udwLabel;
         }

      U_DWORD FAR* pudwTemp=pudwLastLabeledRow;
      pudwLastLabeledRow=pudwCurrentLabelingRow;
      pudwCurrentLabelingRow=pudwTemp;
   }

   GlobalUnlock(hglbBuffer2);
   GlobalFree(hglbBuffer2);
   GlobalUnlock(hglbBuffer1);
   GlobalFree(hglbBuffer1);

   //Allocate a NEWBLOCK array and copy the character-like components--
   //--in the BLOCK array to it.
   HGLOBAL hglbNewBlockArray=GlobalAlloc(GMEM_MOVEABLE,\
                      DWORD(sizeof(NEWBLOCK))*DWORD(MAX_NUM_OF_CHAR_LIKE_BLOCK));
   NEWBLOCK FAR* NewBlockArray=(NEWBLOCK FAR*)GlobalLock(hglbNewBlockArray);

   U_WORD uwBlockNum=0;
   for(U_DWORD udwIndex=udwBlockIndex-1; udwIndex; udwIndex--)
   {
      U_WORD uwBlockWidth=Block[udwIndex].uwMaxEntry-Block[udwIndex].uwMinEntry+1;
      U_WORD uwBlockHeight=Block[udwIndex].uwMaxRow-Block[udwIndex].uwMinRow+1;

      if(Block[udwIndex].uwMinRow && uwBlockWidth<uwImageWidth-2 &&\
         uwBlockWidth<CHARACTER_WIDTH_UPPER_BOUND &&\
         uwBlockHeight<uwImageHeight-2 &&\
         uwBlockHeight<CHARACTER_HEIGHT_UPPER_BOUND &&\
         (uwBlockWidth>NOISE_SIZE || uwBlockHeight>NOISE_SIZE))
      {
         if(uwBlockNum<MAX_NUM_OF_CHAR_LIKE_BLOCK)
         {
            NEWBLOCK FAR* pTemp1=NewBlockArray+uwBlockNum;
            BLOCK* pTemp2=Block+udwIndex;

            pTemp1->uwMinRow=pTemp2->uwMinRow;
            pTemp1->uwMaxRow=pTemp2->uwMaxRow;
            pTemp1->uwMinEntry=pTemp2->uwMinEntry;
            pTemp1->uwMaxEntry=pTemp2->uwMaxEntry;
            pTemp1->ubMeanOfRColorValue=\
                     (pTemp2->ubRColorValueLow+pTemp2->ubRColorValueHigh)/2;
            pTemp1->ubMeanOfGColorValue=\
                     (pTemp2->ubGColorValueLow+pTemp2->ubGColorValueHigh)/2;
            pTemp1->ubMeanOfBColorValue=\
                     (pTemp2->ubBColorValueLow+pTemp2->ubBColorValueHigh)/2;

            uwBlockNum++;
         }
         else
         {
            AfxMessageBox("這份文件中 character-like component 的數目已經超過預設值",
							MB_ICONSTOP);
            uwBlockNum=0;
            udwIndex=1;
         }
      }
   }

   GlobalUnlock(hglbBlockArray);
   GlobalFree(hglbBlockArray);

   //Check if there is error in the above block-searching procedure.
   if(uwBlockNum==0)
   {
      GlobalUnlock(hglbNewBlockArray);
      GlobalFree(hglbNewBlockArray);

      //For debug.
      //Modify the handle and attributes of the image data.
      GlobalUnlock(Image.hglbImage);
      GlobalFree(Image.hglbImage);
      GlobalUnlock(hglbNewImage);
      Image.hglbImage=hglbNewImage;
      Image.iType=2;
      Image.iBytesPerRow=uwNewBytesPerRow;

      //Restore cursor.
      SetCursor(hCursor);

      //For debug.
      char szString[100];
      sprintf(szString, "此圖形檔的最大梯度值為 %d ( < 4590 ? )\n"
			"處理此圖形檔總共花了 %d (%6.2f) 秒", swMaxGradientMag,
			time(NULL)-StartTime, double(clock()-StartClock)/CLOCKS_PER_SEC);
      AfxMessageBox(szString, MB_ICONINFORMATION);

      return 0;
   }

   //The following array is used in the block-classification processing.
   //After applying the maximin-distance algorithm, the blocks in this array--
   //--are the initial cluster centers of the K-means algorithm.
   struct BLOCKCLASSBYCOLOR
   {
      U_BYTE ubRColorValue, ubGColorValue, ubBColorValue;
      U_DWORD udwSumOfR, udwSumOfG, udwSumOfB;
      U_WORD uwMemberNum;
   } BlockClass[MAX_NUM_OF_BLOCK_CLASS];

   //First, choose the first block as the center of the first cluster.
   BlockClass[0].ubRColorValue=NewBlockArray[0].ubMeanOfRColorValue;
   BlockClass[0].ubGColorValue=NewBlockArray[0].ubMeanOfGColorValue;
   BlockClass[0].ubBColorValue=NewBlockArray[0].ubMeanOfBColorValue;
   U_WORD uwNumOfBlockClass=1;

   //Now, apply the maximin-distance algorithm to decide the number of clusters--
   //--and choose the initial cluster centers.
   U_WORD uwTheMostDiffBlock;
   do
   {
      U_WORD uwMaxColorDiff=0;

      for(i=1; i<uwBlockNum; i++)
      {
         NEWBLOCK FAR* pCurrentBlock=NewBlockArray+i;

         int iDiffOfRColorValue=\
                 pCurrentBlock->ubMeanOfRColorValue-BlockClass[0].ubRColorValue;
         if(iDiffOfRColorValue<0)
            iDiffOfRColorValue*=-1;

         int iDiffOfGColorValue=\
                 pCurrentBlock->ubMeanOfGColorValue-BlockClass[0].ubGColorValue;
         if(iDiffOfGColorValue<0)
            iDiffOfGColorValue*=-1;

         int iDiffOfBColorValue=\
                 pCurrentBlock->ubMeanOfBColorValue-BlockClass[0].ubBColorValue;
         if(iDiffOfBColorValue<0)
            iDiffOfBColorValue*=-1;

         U_WORD uwMinColorDiff=iDiffOfRColorValue+iDiffOfGColorValue+iDiffOfBColorValue;

         for(U_WORD j=1; j<uwNumOfBlockClass; j++)
         {
            iDiffOfRColorValue=\
                 pCurrentBlock->ubMeanOfRColorValue-BlockClass[j].ubRColorValue;
            if(iDiffOfRColorValue<0)
               iDiffOfRColorValue*=-1;

            iDiffOfGColorValue=\
                 pCurrentBlock->ubMeanOfGColorValue-BlockClass[j].ubGColorValue;
            if(iDiffOfGColorValue<0)
               iDiffOfGColorValue*=-1;

            iDiffOfBColorValue=\
                 pCurrentBlock->ubMeanOfBColorValue-BlockClass[j].ubBColorValue;
            if(iDiffOfBColorValue<0)
               iDiffOfBColorValue*=-1;

            U_WORD uwColorDiff=iDiffOfRColorValue+iDiffOfGColorValue+iDiffOfBColorValue;

            if(uwColorDiff<uwMinColorDiff)
               uwMinColorDiff=uwColorDiff;
         }

         if(uwMaxColorDiff<uwMinColorDiff)
         {
            uwMaxColorDiff=uwMinColorDiff;
            uwTheMostDiffBlock=i;
         }
      }

      if(uwMaxColorDiff>COLOR_DIFF_THRESHOLD)
      {
         if(uwNumOfBlockClass<MAX_NUM_OF_BLOCK_CLASS)
         {
            BlockClass[uwNumOfBlockClass].ubRColorValue=\
                       NewBlockArray[uwTheMostDiffBlock].ubMeanOfRColorValue;
            BlockClass[uwNumOfBlockClass].ubGColorValue=\
                       NewBlockArray[uwTheMostDiffBlock].ubMeanOfGColorValue;
            BlockClass[uwNumOfBlockClass].ubBColorValue=\
                       NewBlockArray[uwTheMostDiffBlock].ubMeanOfBColorValue;
            uwNumOfBlockClass++;
         }
         else
         {
            AfxMessageBox("這份文件中相同顏色的 character-like component "
					"的數目已經超過預設值", MB_ICONSTOP);
            uwNumOfBlockClass=1;
            break;
         }
      }
      else
         break;
   } while(1);

   //Use the K-means algorithm to classify the blocks.
   S_BYTE sbModified;
   do
   {
      sbModified=0;

      for(i=0; i<uwNumOfBlockClass; i++)
      {
         BlockClass[i].udwSumOfR=0;
         BlockClass[i].udwSumOfG=0;
         BlockClass[i].udwSumOfB=0;
         BlockClass[i].uwMemberNum=0;
      }

      for(i=0; i<uwBlockNum; i++)
      {
         NEWBLOCK FAR* pCurrentBlock=NewBlockArray+i;

         int iDiffOfRColorValue=\
                 pCurrentBlock->ubMeanOfRColorValue-BlockClass[0].ubRColorValue;
         if(iDiffOfRColorValue<0)
            iDiffOfRColorValue*=-1;

         int iDiffOfGColorValue=\
                 pCurrentBlock->ubMeanOfGColorValue-BlockClass[0].ubGColorValue;
         if(iDiffOfGColorValue<0)
            iDiffOfGColorValue*=-1;

         int iDiffOfBColorValue=\
                 pCurrentBlock->ubMeanOfBColorValue-BlockClass[0].ubBColorValue;
         if(iDiffOfBColorValue<0)
            iDiffOfBColorValue*=-1;

         U_WORD uwMinColorDiff=iDiffOfRColorValue+iDiffOfGColorValue+iDiffOfBColorValue;
         U_WORD uwClass=0;

         for(U_WORD j=1; j<uwNumOfBlockClass; j++)
         {
            iDiffOfRColorValue=\
                 pCurrentBlock->ubMeanOfRColorValue-BlockClass[j].ubRColorValue;
            if(iDiffOfRColorValue<0)
               iDiffOfRColorValue*=-1;

            iDiffOfGColorValue=\
                 pCurrentBlock->ubMeanOfGColorValue-BlockClass[j].ubGColorValue;
            if(iDiffOfGColorValue<0)
               iDiffOfGColorValue*=-1;

            iDiffOfBColorValue=\
                 pCurrentBlock->ubMeanOfBColorValue-BlockClass[j].ubBColorValue;
            if(iDiffOfBColorValue<0)
               iDiffOfBColorValue*=-1;

            U_WORD uwColorDiff=iDiffOfRColorValue+iDiffOfGColorValue+iDiffOfBColorValue;

            if(uwColorDiff<uwMinColorDiff)
            {
               uwMinColorDiff=uwColorDiff;
               uwClass=j;
            }
         }

         BlockClass[uwClass].udwSumOfR+=pCurrentBlock->ubMeanOfRColorValue;
         BlockClass[uwClass].udwSumOfG+=pCurrentBlock->ubMeanOfGColorValue;
         BlockClass[uwClass].udwSumOfB+=pCurrentBlock->ubMeanOfBColorValue;
         BlockClass[uwClass].uwMemberNum++;
      }

      for(i=0; i<uwNumOfBlockClass; i++)
      {
         BLOCKCLASSBYCOLOR* pCurrentBlockClass=BlockClass+i;

         U_BYTE ubMeanOfRColorValue=\
                  pCurrentBlockClass->udwSumOfR/pCurrentBlockClass->uwMemberNum;
         if(ubMeanOfRColorValue!=pCurrentBlockClass->ubRColorValue)
         {
            pCurrentBlockClass->ubRColorValue=ubMeanOfRColorValue;
            sbModified=1;
         }

         U_BYTE ubMeanOfGColorValue=\
                  pCurrentBlockClass->udwSumOfG/pCurrentBlockClass->uwMemberNum;
         if(ubMeanOfGColorValue!=pCurrentBlockClass->ubGColorValue)
         {
            pCurrentBlockClass->ubGColorValue=ubMeanOfGColorValue;
            sbModified=1;
         }

         U_BYTE ubMeanOfBColorValue=\
                  pCurrentBlockClass->udwSumOfB/pCurrentBlockClass->uwMemberNum;
         if(ubMeanOfBColorValue!=pCurrentBlockClass->ubBColorValue)
         {
            pCurrentBlockClass->ubBColorValue=ubMeanOfBColorValue;
            sbModified=1;
         }
      }
   } while(sbModified);

   //The following array is used to store the blocks of the same cluster.
   struct CLASSHEAD
   {
      U_WORD uwMemberNum;
      NEWBLOCK* pBlockArray;
   } ClassHead[MAX_NUM_OF_BLOCK_CLASS];

   for(i=0; i<uwNumOfBlockClass; i++)
   {
      ClassHead[i].pBlockArray=new NEWBLOCK[BlockClass[i].uwMemberNum];
      ClassHead[i].uwMemberNum=0;
   }

   for(i=0; i<uwBlockNum; i++)
   {
      NEWBLOCK FAR* pCurrentBlock=NewBlockArray+i;

      int iDiffOfRColorValue=\
              pCurrentBlock->ubMeanOfRColorValue-BlockClass[0].ubRColorValue;
      if(iDiffOfRColorValue<0)
         iDiffOfRColorValue*=-1;

      int iDiffOfGColorValue=\
              pCurrentBlock->ubMeanOfGColorValue-BlockClass[0].ubGColorValue;
      if(iDiffOfGColorValue<0)
         iDiffOfGColorValue*=-1;

      int iDiffOfBColorValue=\
              pCurrentBlock->ubMeanOfBColorValue-BlockClass[0].ubBColorValue;
      if(iDiffOfBColorValue<0)
         iDiffOfBColorValue*=-1;

      U_WORD uwMinColorDiff=iDiffOfRColorValue+iDiffOfGColorValue+iDiffOfBColorValue;
      U_WORD uwClass=0;

      for(U_WORD j=1; j<uwNumOfBlockClass; j++)
      {
         iDiffOfRColorValue=\
              pCurrentBlock->ubMeanOfRColorValue-BlockClass[j].ubRColorValue;
         if(iDiffOfRColorValue<0)
            iDiffOfRColorValue*=-1;

         iDiffOfGColorValue=\
              pCurrentBlock->ubMeanOfGColorValue-BlockClass[j].ubGColorValue;
         if(iDiffOfGColorValue<0)
            iDiffOfGColorValue*=-1;

         iDiffOfBColorValue=\
              pCurrentBlock->ubMeanOfBColorValue-BlockClass[j].ubBColorValue;
         if(iDiffOfBColorValue<0)
            iDiffOfBColorValue*=-1;

         U_WORD uwColorDiff=iDiffOfRColorValue+iDiffOfGColorValue+iDiffOfBColorValue;

         if(uwColorDiff<uwMinColorDiff)
         {
            uwMinColorDiff=uwColorDiff;
            uwClass=j;
         }
      }

      NEWBLOCK* pInsertBlock=\
                  ClassHead[uwClass].pBlockArray+ClassHead[uwClass].uwMemberNum;
      pInsertBlock->uwMinRow=pCurrentBlock->uwMinRow;
      pInsertBlock->uwMaxRow=pCurrentBlock->uwMaxRow;
      pInsertBlock->uwMinEntry=pCurrentBlock->uwMinEntry;
      pInsertBlock->uwMaxEntry=pCurrentBlock->uwMaxEntry;
      pInsertBlock->ubMeanOfRColorValue=pCurrentBlock->ubMeanOfRColorValue;
      pInsertBlock->ubMeanOfGColorValue=pCurrentBlock->ubMeanOfGColorValue;
      pInsertBlock->ubMeanOfBColorValue=pCurrentBlock->ubMeanOfBColorValue;

      ClassHead[uwClass].uwMemberNum++;
   }

   GlobalUnlock(hglbNewBlockArray);
   GlobalFree(hglbNewBlockArray);

   //For debug.
   //Open the c:\temp\working\h.txt file for writing the histograms of R, G, and B of every block.
   FILE* pFile=fopen("c:\\temp\\working\\h.txt", "r+t");
   if(pFile)
      //Move the file pointer to the end of the file.
      fseek(pFile, long(-1), SEEK_END);
   else
      //Build a new file.
      pFile=fopen("c:\\temp\\working\\h.txt", "wt");

   //Reset the pNewBmpImage to white for writing the processing result.
   for(sdwIndex=0; sdwIndex<sdwNewImageVolume; sdwIndex++)
      pNewBmpImage[sdwIndex]=0xff;
/*
   //For debug.
   //Write the classification result to a file.
   U_WORD uwTotalBlock=0;
   for(i=0; i<uwNumOfBlockClass; i++)
   {
//		if(i==7)
//		{
         fprintf(pFile, "\nCLASS %d:\n", i);
         U_WORD uwMemberNum=ClassHead[i].uwMemberNum;
         NEWBLOCK* pCurrentBlockArray=ClassHead[i].pBlockArray;
         for(U_WORD j=0; j<uwMemberNum; j++)
         {
            NEWBLOCK* pCurrentBlock=pCurrentBlockArray+j;
            fprintf(pFile, "BLOCK %d: (R, G, B)=(%d, %d, %d); (x, y)=(%d, %d)\n",\
                    j, pCurrentBlock->ubMeanOfRColorValue,\
                       pCurrentBlock->ubMeanOfGColorValue,\
                       pCurrentBlock->ubMeanOfBColorValue,\
                       (pCurrentBlock->uwMinRow+pCurrentBlock->uwMaxRow)/2,\
                       (pCurrentBlock->uwMinEntry+pCurrentBlock->uwMaxEntry)/2);

            //Mark the block in the image.
            U_WORD uwTop=pCurrentBlock->uwMinRow;
            U_WORD uwTop2=uwTop+1;
            U_WORD uwTop3=uwTop+2;
            U_WORD uwTop4=uwTop+3;
            U_WORD uwTop5=uwTop+4;
            U_WORD uwTop6=uwTop+5;
            U_WORD uwBottom=pCurrentBlock->uwMaxRow;
            U_WORD uwBottom2=uwBottom-1;
            U_WORD uwBottom3=uwBottom-2;
            U_WORD uwBottom4=uwBottom-3;
            U_WORD uwBottom5=uwBottom-4;
            U_WORD uwBottom6=uwBottom-5;
            U_WORD uwLeft=pCurrentBlock->uwMinEntry;
            U_WORD uwLeft2=uwLeft+1;
            U_WORD uwLeft3=uwLeft+2;
            U_WORD uwLeft4=uwLeft+3;
            U_WORD uwLeft5=uwLeft+4;
            U_WORD uwLeft6=uwLeft+5;
            U_WORD uwRight=pCurrentBlock->uwMaxEntry;
            U_WORD uwRight2=uwRight-1;
            U_WORD uwRight3=uwRight-2;
            U_WORD uwRight4=uwRight-3;
            U_WORD uwRight5=uwRight-4;
            U_WORD uwRight6=uwRight-5;

            for(U_WORD k=uwLeft; k<=uwRight; k++)
            {
               ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, uwTop, k);
               ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, uwTop2, k);
               ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, uwTop3, k);
//					ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, uwTop4, k);
//					ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, uwTop5, k);
//					ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, uwTop6, k);
               ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, uwBottom, k);
               ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, uwBottom2, k);
               ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, uwBottom3, k);
//					ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, uwBottom4, k);
//					ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, uwBottom5, k);
//					ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, uwBottom6, k);
            }

            for(k=uwTop; k<=uwBottom; k++)
            {
               ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, k, uwLeft);
               ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, k, uwLeft2);
               ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, k, uwLeft3);
//					ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, k, uwLeft4);
//					ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, k, uwLeft5);
//					ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, k, uwLeft6);
               ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, k, uwRight);
               ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, k, uwRight2);
               ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, k, uwRight3);
//					ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, k, uwRight4);
//					ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, k, uwRight5);
//					ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume, k, uwRight6);
            }
         }

         uwTotalBlock+=uwMemberNum;
//		}
   }
   fprintf(pFile, "\nNUMBER OF BLOCKS: %d\n", uwTotalBlock);
*/
   //Calculate the global parameters.
   uwMaxSmallText=U_WORD(Image.iVertResolution*SMALL_TEXT_HEIGHT_MAX);
   fSmallTextRunLengthMax=float(Image.iHoriResolution*SMALL_TEXT_BLOCK_RUNLENGTH_MAX);
   fLargeTextRunLengthMin=float(Image.iHoriResolution*LARGE_TEXT_BLOCK_RUNLENGTH_MIN);
   fLargeTextRunLengthMax=float(Image.iHoriResolution*LARGE_TEXT_BLOCK_RUNLENGTH_MAX);
   iNoiseRunLength=int(Image.iHoriResolution*NOISE_RUN_LENGTH);
   iNoiseRunLengthInSmallText=int(Image.iHoriResolution*NOISE_RUNLENGTH_IN_SMALL_TEXT_BLOCK);
   uwTextRegionHeightMin=U_WORD(Image.iVertResolution*TEXT_REGION_HEIGHT_MIN);
   uwTextRegionHeightMax=U_WORD(Image.iVertResolution*TEXT_REGION_HEIGHT_MAX);
   uwTextRegionWidthMin=U_WORD(Image.iHoriResolution*TEXT_REGION_WIDTH_MIN);

   for(i=0; i<uwNumOfBlockClass; i++)
   {
//		if(i==3)
//		{
         U_WORD  uwMemberNum=ClassHead[i].uwMemberNum;
         HGLOBAL hglbBlockIndex=GlobalAlloc(GMEM_MOVEABLE,\
                                            sizeof(U_WORD)*uwMemberNum);
         U_WORD FAR* puwBlockIndex=(U_WORD FAR*)GlobalLock(hglbBlockIndex);

         for(U_WORD j=0; j<uwMemberNum; j++)
            puwBlockIndex[j]=j;

         int PASCAL HProjectAndCut(NEWBLOCK*   pBlockArray,\
                                   U_WORD FAR* puwBlockIndex,\
                                   U_WORD      uwBlockNum,\
                                   U_WORD      uwXRange,\
                                   U_WORD      uwYRange,\
                                   REGION*     pXRegion,\
                                   PBMPIMAGE   pMonoBmpImage,\
                                   U_WORD      uwMonoImageRowVolume,\
                                   PBMPIMAGE   pOriBmpImage,\
                                   U_WORD      uwOriRowVolume,\
                                   S_DWORD     sdwOriImageVolume);

         REGION XRegion;
         if(0==HProjectAndCut(ClassHead[i].pBlockArray, puwBlockIndex,\
            uwMemberNum, uwImageHeight, uwImageWidth, &XRegion, pNewBmpImage,\
            uwNewRowVolume, pOriBmpImage, uwOriRowVolume, sdwOriImageVolume))
         {
            int PASCAL VProjectAndCut(NEWBLOCK*   pBlockArray,\
                                      U_WORD FAR* puwBlockIndex,\
                                      U_WORD      uwBlockNum,\
                                      U_WORD      uwXRange,\
                                      U_WORD      uwYRange,\
                                      REGION*     pYRegion,\
                                      PBMPIMAGE   pMonoBmpImage,\
                                      U_WORD      uwMonoImageRowVolume,\
                                      PBMPIMAGE   pOriBmpImage,\
                                      U_WORD      uwOriRowVolume,\
                                      S_DWORD     sdwOriImageVolume);

            REGION YRegion;
            if(0==VProjectAndCut(ClassHead[i].pBlockArray, puwBlockIndex,\
               uwMemberNum, XRegion.uwHigh+1, uwImageWidth, &YRegion,\
               pNewBmpImage, uwNewRowVolume, pOriBmpImage, uwOriRowVolume,\
               sdwOriImageVolume))
            {
               //Means that there is no further cutting in the X- and Y-direction.
               //Thus, check if this group of blocks is a text string.
               if(uwMemberNum==1)
			   {
				  GlobalUnlock(hglbBlockIndex);
				  GlobalFree(hglbBlockIndex);
				  delete[] ClassHead[i].pBlockArray;
                  continue;
			   }

               U_WORD uwRegionHeight=XRegion.uwHigh-XRegion.uwLow+1;
               U_WORD uwRegionWidth=YRegion.uwHigh-YRegion.uwLow+1;
               float fAspectRatio=float(uwRegionWidth)/float(uwRegionHeight);
               if(uwRegionHeight<uwTextRegionHeightMin ||
				  uwRegionHeight>uwTextRegionHeightMax ||
                  uwRegionWidth<uwTextRegionWidthMin ||
                  fAspectRatio<TEXT_REGION_ASPECT_RATIO_MIN ||
				  uwMemberNum>fAspectRatio*PARAMETER2)
			   {
				  GlobalUnlock(hglbBlockIndex);
				  GlobalFree(hglbBlockIndex);
				  delete[] ClassHead[i].pBlockArray;
                  continue;
			   }

               U_DWORD   udwTotalBlockArea=0;
               U_WORD    uwBlockWidthBound=U_WORD(double(uwRegionHeight)*CHAR_ASPECT);
               NEWBLOCK* pBlockArray=ClassHead[i].pBlockArray;
               for(j=0; j<uwMemberNum; j++)
               {
                  U_WORD uwBlockWidth=pBlockArray[j].uwMaxEntry-\
                                      pBlockArray[j].uwMinEntry+1;
                  if(uwBlockWidth>uwBlockWidthBound)
                  {
                     udwTotalBlockArea=0;
                     break;
                  }

                  udwTotalBlockArea+=U_DWORD(uwBlockWidth)*U_DWORD(pBlockArray[j].uwMaxRow-\
                                     pBlockArray[j].uwMinRow+1);
               }

               if(udwTotalBlockArea<U_DWORD(uwRegionWidth)*U_DWORD(uwRegionHeight)*\
                                    BLOCK_SATURATION)
			   {
				  GlobalUnlock(hglbBlockIndex);
				  GlobalFree(hglbBlockIndex);
				  delete[] ClassHead[i].pBlockArray;
                  continue;
			   }

               //If the group of blocks meets the above requirements,--
               //--it is a text string. So, further transform them into--
               //--the white-background/black-text format.
               //First, determine the color of text.
               U_DWORD udwRColorValue=0, udwGColorValue=0, udwBColorValue=0;
               for(j=0; j<uwMemberNum; j++)
               {
                  udwRColorValue+=pBlockArray[j].ubMeanOfRColorValue;
                  udwGColorValue+=pBlockArray[j].ubMeanOfGColorValue;
                  udwBColorValue+=pBlockArray[j].ubMeanOfBColorValue;
               }

               udwRColorValue/=uwMemberNum;
               udwGColorValue/=uwMemberNum;
               udwBColorValue/=uwMemberNum;

			   //Create a binary image of the block for further testing.
			   U_WORD uwRowVolume= uwRegionWidth%8 ?
								uwRegionWidth/8+1 : uwRegionWidth/8;
			   HGLOBAL hglbRegionImage=GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
									DWORD(uwRowVolume)*DWORD(uwRegionHeight));
			   PBMPIMAGE pRegionImage=(PBMPIMAGE)GlobalLock(hglbRegionImage);

               sdwBase=sdwOriImageVolume-S_DWORD(uwOriRowVolume)*\
                       S_DWORD(XRegion.uwLow+1)+S_DWORD(YRegion.uwLow)*3;
               for(j=0; j<uwRegionHeight; j++, sdwBase-=uwOriRowVolume)
               {
                  sdwIndex=sdwBase;
                  for(U_WORD k=0; k<uwRegionWidth; k++, sdwIndex+=3)
                  {
                     //Calculate the color difference between this pixel and--
                     //--the text color.
                     int iRDiff, iGDiff, iBDiff;
                     if(pOriBmpImage[sdwIndex+2]>udwRColorValue)
                        iRDiff=pOriBmpImage[sdwIndex+2]-udwRColorValue;
                     else
                        iRDiff=udwRColorValue-pOriBmpImage[sdwIndex+2];

                     if(pOriBmpImage[sdwIndex+1]>udwGColorValue)
                        iGDiff=pOriBmpImage[sdwIndex+1]-udwGColorValue;
                     else
                        iGDiff=udwGColorValue-pOriBmpImage[sdwIndex+1];

                     if(pOriBmpImage[sdwIndex]>udwBColorValue)
                        iBDiff=pOriBmpImage[sdwIndex]-udwBColorValue;
                     else
                        iBDiff=udwBColorValue-pOriBmpImage[sdwIndex];

                     //Check if this pixel belongs to the background.
                     if(iRDiff+iGDiff+iBDiff>=COLOR_DIFFUSION)
                        //Means this pixel belongs to the background, so set it to white.
                        SetMonoPCXPixel(pRegionImage, uwRowVolume, j, k);
                  }
               }

			   if(uwRegionHeight<=uwMaxSmallText)
			   {
				  int SmallTextBlockVerification2(PBMPIMAGE pImage,
												  U_WORD    uwImageWidth,
												  U_WORD    uwImageHeight,
												  U_WORD    uwRowVolume,
												  float	   fSmallTextRunLengthMax,
												  int	   iNoiseRunLength);

				  if(SmallTextBlockVerification2(pRegionImage, uwRegionWidth,
												 uwRegionHeight, uwRowVolume,
												 fSmallTextRunLengthMax, iNoiseRunLength))
				  {
					 //This is a text block. Transform it into the
					 //  white-background/black-text format.
					 U_WORD uwRow=XRegion.uwLow;
					 for(j=0; j<uwRegionHeight; j++, uwRow++)
						for(U_WORD k=0, uwEntry=YRegion.uwLow;
							k<uwRegionWidth; k++, uwEntry++)
						   if(GetMonoPCXPixel(pRegionImage, uwRowVolume, j, k)==0)
							  ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume,
												uwRow, uwEntry);
				  }
			   }
			   else
			   {
				  int LargeTextBlockVerification2(PBMPIMAGE pImage,
												  U_WORD    uwImageWidth,
												  U_WORD    uwImageHeight,
												  U_WORD    uwRowVolume,
												  float	   fRunLengthMin,
												  float	   fRunLengthMax,
												  int	   iNoiseRunLength);

				  if(LargeTextBlockVerification2(pRegionImage, uwRegionWidth,
												 uwRegionHeight, uwRowVolume,
												 fLargeTextRunLengthMin,
												 fLargeTextRunLengthMax, iNoiseRunLength))
				  {
					 //This is a text block. Transform it into the
					 //  white-background/black-text format.
					 U_WORD uwRow=XRegion.uwLow;
					 for(j=0; j<uwRegionHeight; j++, uwRow++)
						for(U_WORD k=0, uwEntry=YRegion.uwLow;
							k<uwRegionWidth; k++, uwEntry++)
						   if(GetMonoPCXPixel(pRegionImage, uwRowVolume, j, k)==0)
							  ResetMonoPCXPixel(pNewBmpImage, uwNewRowVolume,
												uwRow, uwEntry);
				  }
			   }

			   GlobalUnlock(hglbRegionImage);
			   GlobalFree(hglbRegionImage);
            }
         }

         GlobalUnlock(hglbBlockIndex);
         GlobalFree(hglbBlockIndex);
//		}

      delete[] ClassHead[i].pBlockArray;
   }

   //For debug.
   //Close the c:\temp\working\h.txt file.
   fputc(0x0a, pFile);
   fputc(0x1a, pFile);
   fclose(pFile);

   //For debug.
   //Modify the image handle and the PCX image file header in the PCXImage class.
   GlobalUnlock(Image.hglbImage);
   GlobalFree(Image.hglbImage);
   GlobalUnlock(hglbNewImage);
   Image.hglbImage=hglbNewImage;
   Image.iType=2;
   Image.iBytesPerRow=uwNewBytesPerRow;

   //Restore cursor.
   SetCursor(hCursor);

   //For debug.
   char szString[100];
   sprintf(szString, "此圖形檔的最大梯度值為 %d ( < 4590 ? )\n"
			"處理此圖形檔總共花了 %d (%6.2f) 秒", swMaxGradientMag,
			time(NULL)-StartTime, double(clock()-StartClock)/CLOCKS_PER_SEC);
//   AfxMessageBox(szString, MB_ICONINFORMATION);

   return 0;
}

int PASCAL HProjectAndCut(NEWBLOCK*   pBlockArray,\
                          U_WORD FAR* puwBlockIndex,\
                          U_WORD      uwBlockNum,\
                          U_WORD      uwXRange,\
                          U_WORD      uwYRange,\
                          REGION*     pXRegion,\
                          PBMPIMAGE   pMonoBmpImage,\
                          U_WORD      uwMonoImageRowVolume,\
                          PBMPIMAGE   pOriBmpImage,\
                          U_WORD      uwOriRowVolume,\
                          S_DWORD     sdwOriImageVolume)
{
   //Allocate an array and reset it to store the horizontal projection.
   U_BYTE* pubHProjection=new U_BYTE[uwXRange];
   for(U_WORD i=0; i<uwXRange; i++)
      pubHProjection[i]=0;

   for(i=0; i<uwBlockNum; i++)
   {
      U_WORD uwEnd=pBlockArray[puwBlockIndex[i]].uwMaxRow;
      for(U_WORD j=pBlockArray[puwBlockIndex[i]].uwMinRow; j<=uwEnd; j++)
         pubHProjection[j]=1;
   }

   REGION XRegion[MAX_REGION_NUM];  //       --------> Y
                                    //      |
                                    //      |
                                    //      |
                                    //    X |

   //Search the regions in the projection.
   U_BYTE ubRegionNum=0;
   i=0;
   do
   {
      for(; pubHProjection[i]==0 && i<uwXRange; i++);  //Skip the leading 0's.
      XRegion[ubRegionNum].uwLow=i;  //Record the position of the first 1.
      for(; pubHProjection[i] && i<uwXRange; i++);  //Search the last 1 of this sequence.

      if(i!=XRegion[ubRegionNum].uwLow)
         if(ubRegionNum<MAX_REGION_NUM)
         {
            XRegion[ubRegionNum].uwHigh=i-1;
            ubRegionNum++;
         }
         else
         {
            delete[] pubHProjection;
            AfxMessageBox("作RXYC時,找到的region數目超過預設值", MB_ICONSTOP);
            return 1;
         }
   } while(i<uwXRange);

   delete[] pubHProjection;

   if(ubRegionNum==1)
   {
      pXRegion->uwLow=XRegion[0].uwLow;
      pXRegion->uwHigh=XRegion[0].uwHigh;
      return 0;  //Means that no further cut is achieved.
   }

   //Allocate a CLASSHEAD array to store the result of cut.
   struct CLASSHEAD
   {
      U_WORD  uwMemberNum;
      HGLOBAL hglbBlockIndex;
      U_WORD FAR* puwBlockIndex;
   };

   HGLOBAL hglbClassHead=GlobalAlloc(GMEM_MOVEABLE,\
                                     sizeof(CLASSHEAD)*ubRegionNum);
   CLASSHEAD FAR* ClassHead=(CLASSHEAD FAR*)GlobalLock(hglbClassHead);
   for(i=0; i<ubRegionNum; i++)
   {
      ClassHead[i].hglbBlockIndex=GlobalAlloc(GMEM_MOVEABLE,\
                                              sizeof(U_WORD)*MAX_MEMBER_NUM);
      ClassHead[i].puwBlockIndex=(U_WORD FAR*)GlobalLock(ClassHead[i].hglbBlockIndex);
      ClassHead[i].uwMemberNum=0;
   }

   //Separate blocks according to their locations.
   for(i=0; i<uwBlockNum; i++)
   {
      U_WORD uwBlockLocation=pBlockArray[puwBlockIndex[i]].uwMinRow;
      for(U_WORD j=0; j<ubRegionNum; j++)
         if(uwBlockLocation>=XRegion[j].uwLow &&\
            uwBlockLocation<=XRegion[j].uwHigh)
         {
            if(ClassHead[j].uwMemberNum<MAX_MEMBER_NUM)
            {
               (ClassHead[j].puwBlockIndex)[ClassHead[j].uwMemberNum]=puwBlockIndex[i];
               ClassHead[j].uwMemberNum++;
               break;
            }
            else
            {
               for(i=0; i<ubRegionNum; i++)
               {
                  GlobalUnlock(ClassHead[i].hglbBlockIndex);
                  GlobalFree(ClassHead[i].hglbBlockIndex);
               }

               GlobalUnlock(hglbClassHead);
               GlobalFree(hglbClassHead);

               AfxMessageBox("作RXYC時,某個region內的block數目超過預設值",
								MB_ICONSTOP);
               return 1;
            }
         }
   }

   //For each group of blocks, invoke VProjectAndCut(...) to try to further separate them.
   for(i=0; i<ubRegionNum; i++)
   {
      REGION YRegion;
      int PASCAL VProjectAndCut(NEWBLOCK*   pBlockArray,\
                                U_WORD FAR* puwBlockIndex,\
                                U_WORD      uwBlockNum,\
                                U_WORD      uwXRange,\
                                U_WORD      uwYRange,\
                                REGION*     pYRegion,\
                                PBMPIMAGE   pMonoBmpImage,\
                                U_WORD      uwMonoImageRowVolume,\
                                PBMPIMAGE   pOriBmpImage,\
                                U_WORD      uwOriRowVolume,\
                                S_DWORD     sdwOriImageVolume);
      if(0==VProjectAndCut(pBlockArray, ClassHead[i].puwBlockIndex,\
            ClassHead[i].uwMemberNum, XRegion[i].uwHigh+1, uwYRange, &YRegion,\
            pMonoBmpImage, uwMonoImageRowVolume, pOriBmpImage, uwOriRowVolume,\
            sdwOriImageVolume))
      {
         //Means that there is no further cutting in the Y-direction.
         //Thus, check if this group of blocks is a text string.
         if(ClassHead[i].uwMemberNum==1)
            continue;

         U_WORD uwRegionHeight=XRegion[i].uwHigh-XRegion[i].uwLow+1;
         U_WORD uwRegionWidth=YRegion.uwHigh-YRegion.uwLow+1;
         float fAspectRatio=float(uwRegionWidth)/float(uwRegionHeight);
         if(uwRegionHeight<uwTextRegionHeightMin ||
			uwRegionHeight>uwTextRegionHeightMax ||
            uwRegionWidth<uwTextRegionWidthMin ||
            fAspectRatio<TEXT_REGION_ASPECT_RATIO_MIN)
            continue;

         if(ClassHead[i].uwMemberNum>fAspectRatio*PARAMETER2)
            continue;

         U_WORD  uwMemberNum=ClassHead[i].uwMemberNum;
         U_WORD FAR* puwCurrentBlockIndex=ClassHead[i].puwBlockIndex;
         U_DWORD udwTotalBlockArea=0;
         U_WORD  uwBlockWidthBound=U_WORD(double(uwRegionHeight)*CHAR_ASPECT);
         for(U_WORD j=0; j<uwMemberNum; j++)
         {
            U_WORD uwBlockWidth=pBlockArray[puwCurrentBlockIndex[j]].uwMaxEntry-\
                                pBlockArray[puwCurrentBlockIndex[j]].uwMinEntry+1;
            if(uwBlockWidth>uwBlockWidthBound)
            {
               udwTotalBlockArea=0;
               break;
            }

            udwTotalBlockArea+=U_DWORD(uwBlockWidth)*\
                               U_DWORD(pBlockArray[puwCurrentBlockIndex[j]].uwMaxRow-\
                                pBlockArray[puwCurrentBlockIndex[j]].uwMinRow+1);
         }

         if(udwTotalBlockArea<U_DWORD(uwRegionWidth)*U_DWORD(uwRegionHeight)*\
                              BLOCK_SATURATION)
            continue;

         //If the group of blocks meets the above requirements,--
         //--it is a text string. So, further transform them into--
         //--the white-background/black-text format.
         //First, determine the color of text.
         U_DWORD udwRColorValue=0, udwGColorValue=0, udwBColorValue=0;
         for(j=0; j<uwMemberNum; j++)
         {
            udwRColorValue+=pBlockArray[puwCurrentBlockIndex[j]].ubMeanOfRColorValue;
            udwGColorValue+=pBlockArray[puwCurrentBlockIndex[j]].ubMeanOfGColorValue;
            udwBColorValue+=pBlockArray[puwCurrentBlockIndex[j]].ubMeanOfBColorValue;
         }

         udwRColorValue/=uwMemberNum;
         udwGColorValue/=uwMemberNum;
         udwBColorValue/=uwMemberNum;

		 //Create a binary image of the block for further testing.
		 U_WORD uwRowVolume= uwRegionWidth%8 ?
							uwRegionWidth/8+1 : uwRegionWidth/8;
		 HGLOBAL hglbRegionImage=GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
								DWORD(uwRowVolume)*DWORD(uwRegionHeight));
		 PBMPIMAGE pRegionImage=(PBMPIMAGE)GlobalLock(hglbRegionImage);

         S_DWORD sdwBase=sdwOriImageVolume-S_DWORD(uwOriRowVolume)*\
                         S_DWORD(XRegion[i].uwLow+1)+S_DWORD(YRegion.uwLow)*3;
         for(j=0; j<uwRegionHeight; j++, sdwBase-=uwOriRowVolume)
         {
            S_DWORD sdwIndex=sdwBase;
            for(U_WORD k=0; k<uwRegionWidth; k++, sdwIndex+=3)
            {
               //Calculate the color difference between this pixel and--
               //--the text color.
               int iRDiff, iGDiff, iBDiff;
               if(pOriBmpImage[sdwIndex+2]>udwRColorValue)
                  iRDiff=pOriBmpImage[sdwIndex+2]-udwRColorValue;
               else
                  iRDiff=udwRColorValue-pOriBmpImage[sdwIndex+2];

               if(pOriBmpImage[sdwIndex+1]>udwGColorValue)
                  iGDiff=pOriBmpImage[sdwIndex+1]-udwGColorValue;
               else
                  iGDiff=udwGColorValue-pOriBmpImage[sdwIndex+1];

               if(pOriBmpImage[sdwIndex]>udwBColorValue)
                  iBDiff=pOriBmpImage[sdwIndex]-udwBColorValue;
               else
                  iBDiff=udwBColorValue-pOriBmpImage[sdwIndex];

               //Check if this pixel belongs to the background.
               if(iRDiff+iGDiff+iBDiff>=COLOR_DIFFUSION)
                  //Means this pixel belongs to the background. Set it to white.
				  SetMonoPCXPixel(pRegionImage, uwRowVolume, j, k);
			}
         }

		 if(uwRegionHeight<=uwMaxSmallText)
		 {
			int SmallTextBlockVerification2(PBMPIMAGE pImage,
											U_WORD    uwImageWidth,
											U_WORD    uwImageHeight,
											U_WORD    uwRowVolume,
											float	 fSmallTextRunLengthMax,
											int		 iNoiseRunLength);

			if(SmallTextBlockVerification2(pRegionImage, uwRegionWidth,
										   uwRegionHeight, uwRowVolume,
										   fSmallTextRunLengthMax, iNoiseRunLength))
			{
			   //This is a text block. Transform it into the
			   //  white-background/black-text format.
			   U_WORD uwRow=XRegion[i].uwLow;
			   for(j=0; j<uwRegionHeight; j++, uwRow++)
				  for(U_WORD k=0, uwEntry=YRegion.uwLow;
					  k<uwRegionWidth; k++, uwEntry++)
					 if(GetMonoPCXPixel(pRegionImage, uwRowVolume, j, k)==0)
						ResetMonoPCXPixel(pMonoBmpImage, uwMonoImageRowVolume,
										  uwRow, uwEntry);
			}
		 }
		 else
		 {
			int LargeTextBlockVerification2(PBMPIMAGE pImage,
											U_WORD    uwImageWidth,
											U_WORD    uwImageHeight,
											U_WORD    uwRowVolume,
											float	   fRunLengthMin,
											float	   fRunLengthMax,
											int	   iNoiseRunLength);

			if(LargeTextBlockVerification2(pRegionImage, uwRegionWidth,
										   uwRegionHeight, uwRowVolume,
										   fLargeTextRunLengthMin,
										   fLargeTextRunLengthMax, iNoiseRunLength))
			{
			   //This is a text block. Transform it into the
			   //  white-background/black-text format.
			   U_WORD uwRow=XRegion[i].uwLow;
			   for(j=0; j<uwRegionHeight; j++, uwRow++)
				  for(U_WORD k=0, uwEntry=YRegion.uwLow;
					  k<uwRegionWidth; k++, uwEntry++)
					 if(GetMonoPCXPixel(pRegionImage, uwRowVolume, j, k)==0)
						ResetMonoPCXPixel(pMonoBmpImage, uwMonoImageRowVolume,
										  uwRow, uwEntry);
			}
		 }

		 GlobalUnlock(hglbRegionImage);
		 GlobalFree(hglbRegionImage);
      }
   }

   for(i=0; i<ubRegionNum; i++)
   {
      GlobalUnlock(ClassHead[i].hglbBlockIndex);
      GlobalFree(ClassHead[i].hglbBlockIndex);
   }

   GlobalUnlock(hglbClassHead);
   GlobalFree(hglbClassHead);

   return 1;
}

int PASCAL VProjectAndCut(NEWBLOCK*   pBlockArray,\
                          U_WORD FAR* puwBlockIndex,\
                          U_WORD      uwBlockNum,\
                          U_WORD      uwXRange,\
                          U_WORD      uwYRange,\
                          REGION*     pYRegion,\
                          PBMPIMAGE   pMonoBmpImage,\
                          U_WORD      uwMonoImageRowVolume,\
                          PBMPIMAGE   pOriBmpImage,\
                          U_WORD      uwOriRowVolume,\
                          S_DWORD     sdwOriImageVolume)
{
   //Allocate an array and reset it to store the vertical projection.
   U_BYTE* pubVProjection=new U_BYTE[uwYRange];
   for(U_WORD i=0; i<uwYRange; i++)
      pubVProjection[i]=0;

   for(i=0; i<uwBlockNum; i++)
   {
      U_WORD uwEnd=pBlockArray[puwBlockIndex[i]].uwMaxEntry;
      for(U_WORD j=pBlockArray[puwBlockIndex[i]].uwMinEntry; j<=uwEnd; j++)
         pubVProjection[j]=1;
   }

   REGION YRegion[MAX_REGION_NUM];  //       --------> Y
                                    //      |
                                    //      |
                                    //      |
                                    //    X |

   //Search the regions in the projection.
   U_BYTE ubRegionNum=0;
   i=0;
   U_BYTE ubSettingRegion=0;
   U_WORD uwMaxLengthOf1;
   for(; pubVProjection[i]==0; i++);  //Skip the leading 0's.
   do
   {
      U_WORD uwLength;

      if(!ubSettingRegion)
      {
         ubSettingRegion=1;
         YRegion[ubRegionNum].uwLow=i;
         uwMaxLengthOf1=50;  //Set the initialize value.
      }

      //Calculate the length of the sequence of 1's.
      for(uwLength=0; pubVProjection[i] && i<uwYRange; i++, uwLength++);
      if(uwMaxLengthOf1<uwLength)
         uwMaxLengthOf1=uwLength;

      YRegion[ubRegionNum].uwHigh=i-1;

      //Calculate the length of the sequence of 0's.
      for(uwLength=0; pubVProjection[i]==0 && i<uwYRange; i++, uwLength++);
      if(uwMaxLengthOf1<uwLength || i==uwYRange)
         if(ubRegionNum<MAX_REGION_NUM)
         {
            ubSettingRegion=0;
            ubRegionNum++;
         }
         else
         {
            delete[] pubVProjection;
            AfxMessageBox("作RXYC時,找到的region數目超過預設值", MB_ICONSTOP);
            return 1;
         }
   } while(i<uwYRange);

   delete[] pubVProjection;

   if(ubRegionNum==1)
   {
      pYRegion->uwLow=YRegion[0].uwLow;
      pYRegion->uwHigh=YRegion[0].uwHigh;
      return 0;  //Means that no further cut is achieved.
   }

   //Allocate a CLASSHEAD array to store the result of cut.
   struct CLASSHEAD
   {
      U_WORD  uwMemberNum;
      HGLOBAL hglbBlockIndex;
      U_WORD FAR* puwBlockIndex;
   };

   HGLOBAL hglbClassHead=GlobalAlloc(GMEM_MOVEABLE,\
                                     sizeof(CLASSHEAD)*ubRegionNum);
   CLASSHEAD FAR* ClassHead=(CLASSHEAD FAR*)GlobalLock(hglbClassHead);
   for(i=0; i<ubRegionNum; i++)
   {
      ClassHead[i].hglbBlockIndex=GlobalAlloc(GMEM_MOVEABLE,\
                                              sizeof(U_WORD)*MAX_MEMBER_NUM);
      ClassHead[i].puwBlockIndex=(U_WORD FAR*)GlobalLock(ClassHead[i].hglbBlockIndex);
      ClassHead[i].uwMemberNum=0;
   }

   //Separate blocks according to their locations.
   for(i=0; i<uwBlockNum; i++)
   {
      U_WORD uwBlockLocation=pBlockArray[puwBlockIndex[i]].uwMinEntry;
      for(U_WORD j=0; j<ubRegionNum; j++)
         if(uwBlockLocation>=YRegion[j].uwLow &&\
            uwBlockLocation<=YRegion[j].uwHigh)
         {
            if(ClassHead[j].uwMemberNum<MAX_MEMBER_NUM)
            {
               (ClassHead[j].puwBlockIndex)[ClassHead[j].uwMemberNum]=puwBlockIndex[i];
               ClassHead[j].uwMemberNum++;
               break;
            }
            else
            {
               for(i=0; i<ubRegionNum; i++)
               {
                  GlobalUnlock(ClassHead[i].hglbBlockIndex);
                  GlobalFree(ClassHead[i].hglbBlockIndex);
               }

               GlobalUnlock(hglbClassHead);
               GlobalFree(hglbClassHead);

               AfxMessageBox("作RXYC時,某個region內的block數目超過預設值",
								MB_ICONSTOP);
               return 1;
            }
         }
   }

   //For each group of blocks, invoke HProjectAndCut(...) to try to further separate them.
   for(i=0; i<ubRegionNum; i++)
   {
      REGION XRegion;
      int PASCAL HProjectAndCut(NEWBLOCK*   pBlockArray,\
                                U_WORD FAR* puwBlockIndex,\
                                U_WORD      uwBlockNum,\
                                U_WORD      uwXRange,\
                                U_WORD      uwYRange,\
                                REGION*     pXRegion,\
                                PBMPIMAGE   pMonoBmpImage,\
                                U_WORD      uwMonoImageRowVolume,\
                                PBMPIMAGE   pOriBmpImage,\
                                U_WORD      uwOriRowVolume,\
                                S_DWORD     sdwOriImageVolume);
      if(0==HProjectAndCut(pBlockArray, ClassHead[i].puwBlockIndex,\
            ClassHead[i].uwMemberNum, uwXRange, YRegion[i].uwHigh+1, &XRegion,\
            pMonoBmpImage, uwMonoImageRowVolume, pOriBmpImage, uwOriRowVolume,\
            sdwOriImageVolume))
      {
         //Means that there is no further cutting in the X-direction.
         //Thus, check if this group of blocks is a text string.
         if(ClassHead[i].uwMemberNum==1)
            continue;

         U_WORD uwRegionHeight=XRegion.uwHigh-XRegion.uwLow+1;
         U_WORD uwRegionWidth=YRegion[i].uwHigh-YRegion[i].uwLow+1;
         float fAspectRatio=float(uwRegionWidth)/float(uwRegionHeight);
         if(uwRegionHeight<uwTextRegionHeightMin ||
			uwRegionHeight>uwTextRegionHeightMax ||
            uwRegionWidth<uwTextRegionWidthMin ||
            fAspectRatio<TEXT_REGION_ASPECT_RATIO_MIN)
            continue;

         if(ClassHead[i].uwMemberNum>fAspectRatio*PARAMETER2)
            continue;

         U_WORD  uwMemberNum=ClassHead[i].uwMemberNum;
         U_WORD FAR* puwCurrentBlockIndex=ClassHead[i].puwBlockIndex;
         U_DWORD udwTotalBlockArea=0;
         U_WORD  uwBlockWidthBound=U_WORD(double(uwRegionHeight)*CHAR_ASPECT);
         for(U_WORD j=0; j<uwMemberNum; j++)
         {
            U_WORD uwBlockWidth=pBlockArray[puwCurrentBlockIndex[j]].uwMaxEntry-\
                                pBlockArray[puwCurrentBlockIndex[j]].uwMinEntry+1;
            if(uwBlockWidth>uwBlockWidthBound)
            {
               udwTotalBlockArea=0;
               break;
            }

            udwTotalBlockArea+=U_DWORD(uwBlockWidth)*\
                               U_DWORD(pBlockArray[puwCurrentBlockIndex[j]].uwMaxRow-\
                                pBlockArray[puwCurrentBlockIndex[j]].uwMinRow+1);
         }

         if(udwTotalBlockArea<U_DWORD(uwRegionWidth)*U_DWORD(uwRegionHeight)*\
                              BLOCK_SATURATION)
            continue;

         //If the group of blocks meets the above requirements,--
         //--it is a text string. So, further transform them into--
         //--the white-background/black-text format.
         //First, determine the color of text.
         U_DWORD udwRColorValue=0, udwGColorValue=0, udwBColorValue=0;
         for(j=0; j<uwMemberNum; j++)
         {
            udwRColorValue+=pBlockArray[puwCurrentBlockIndex[j]].ubMeanOfRColorValue;
            udwGColorValue+=pBlockArray[puwCurrentBlockIndex[j]].ubMeanOfGColorValue;
            udwBColorValue+=pBlockArray[puwCurrentBlockIndex[j]].ubMeanOfBColorValue;
         }

         udwRColorValue/=uwMemberNum;
         udwGColorValue/=uwMemberNum;
         udwBColorValue/=uwMemberNum;

		 //Create a binary image of the block for further testing.
		 U_WORD uwRowVolume= uwRegionWidth%8 ?
							uwRegionWidth/8+1 : uwRegionWidth/8;
		 HGLOBAL hglbRegionImage=GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
								DWORD(uwRowVolume)*DWORD(uwRegionHeight));
		 PBMPIMAGE pRegionImage=(PBMPIMAGE)GlobalLock(hglbRegionImage);

         S_DWORD sdwBase=sdwOriImageVolume-S_DWORD(uwOriRowVolume)*\
                         S_DWORD(XRegion.uwLow+1)+S_DWORD(YRegion[i].uwLow)*3;
         for(j=0; j<uwRegionHeight; j++, sdwBase-=uwOriRowVolume)
         {
            S_DWORD sdwIndex=sdwBase;
            for(U_WORD k=0; k<uwRegionWidth; k++, sdwIndex+=3)
            {
               //Calculate the color difference between this pixel and--
               //--the text color.
               int iRDiff, iGDiff, iBDiff;
               if(pOriBmpImage[sdwIndex+2]>udwRColorValue)
                  iRDiff=pOriBmpImage[sdwIndex+2]-udwRColorValue;
               else
                  iRDiff=udwRColorValue-pOriBmpImage[sdwIndex+2];

               if(pOriBmpImage[sdwIndex+1]>udwGColorValue)
                  iGDiff=pOriBmpImage[sdwIndex+1]-udwGColorValue;
               else
                  iGDiff=udwGColorValue-pOriBmpImage[sdwIndex+1];

               if(pOriBmpImage[sdwIndex]>udwBColorValue)
                  iBDiff=pOriBmpImage[sdwIndex]-udwBColorValue;
               else
                  iBDiff=udwBColorValue-pOriBmpImage[sdwIndex];

               //Check if this pixel belongs to the background.
               if(iRDiff+iGDiff+iBDiff>=COLOR_DIFFUSION)
                  //Means this pixel belongs to the background. Set it to white.
                  SetMonoPCXPixel(pRegionImage, uwRowVolume, j, k);
            }
         }

		 if(uwRegionHeight<=uwMaxSmallText)
		 {
			int SmallTextBlockVerification2(PBMPIMAGE pImage,
											U_WORD    uwImageWidth,
											U_WORD    uwImageHeight,
											U_WORD    uwRowVolume,
											float	 fSmallTextRunLengthMax,
											int		 iNoiseRunLength);

			if(SmallTextBlockVerification2(pRegionImage, uwRegionWidth,
										   uwRegionHeight, uwRowVolume,
										   fSmallTextRunLengthMax, iNoiseRunLength))
			{
			   //This is a text block. Transform it into the
			   //  white-background/black-text format.
			   U_WORD uwRow=XRegion.uwLow;
			   for(j=0; j<uwRegionHeight; j++, uwRow++)
				  for(U_WORD k=0, uwEntry=YRegion[i].uwLow;
					  k<uwRegionWidth; k++, uwEntry++)
					 if(GetMonoPCXPixel(pRegionImage, uwRowVolume, j, k)==0)
						ResetMonoPCXPixel(pMonoBmpImage, uwMonoImageRowVolume,
										  uwRow, uwEntry);
			}
		 }
		 else
		 {
			int LargeTextBlockVerification2(PBMPIMAGE pImage,
											U_WORD    uwImageWidth,
											U_WORD    uwImageHeight,
											U_WORD    uwRowVolume,
											float	   fRunLengthMin,
											float	   fRunLengthMax,
											int	   iNoiseRunLength);

			if(LargeTextBlockVerification2(pRegionImage, uwRegionWidth,
										   uwRegionHeight, uwRowVolume,
										   fLargeTextRunLengthMin,
										   fLargeTextRunLengthMax, iNoiseRunLength))
			{
			   //This is a text block. Transform it into the
			   //  white-background/black-text format.
			   U_WORD uwRow=XRegion.uwLow;
			   for(j=0; j<uwRegionHeight; j++, uwRow++)
				  for(U_WORD k=0, uwEntry=YRegion[i].uwLow;
					  k<uwRegionWidth; k++, uwEntry++)
					 if(GetMonoPCXPixel(pRegionImage, uwRowVolume, j, k)==0)
						ResetMonoPCXPixel(pMonoBmpImage, uwMonoImageRowVolume,
										  uwRow, uwEntry);
			}
		 }

		 GlobalUnlock(hglbRegionImage);
		 GlobalFree(hglbRegionImage);
      }
   }

   for(i=0; i<ubRegionNum; i++)
   {
      GlobalUnlock(ClassHead[i].hglbBlockIndex);
      GlobalFree(ClassHead[i].hglbBlockIndex);
   }

   GlobalUnlock(hglbClassHead);
   GlobalFree(hglbClassHead);

   return 1;
}

int SmallTextBlockVerification2(PBMPIMAGE pImage,
								U_WORD    uwImageWidth,
								U_WORD    uwImageHeight,
								U_WORD    uwRowVolume,
								float	 fRunLengthMax,
								int		 iNoiseRunLength)
{
	if(uwImageWidth>MAX_BLOCK_WIDTH)
	{
		AfxMessageBox("Block 的寬度超過 MAX_BLOCK_WIDTH 的設定值，\n"
					  "該 block 將被忽略。", MB_ICONINFORMATION);
		return 0;
	}

	char VertProjection[MAX_BLOCK_WIDTH];  //To store the vertical profile.
	for(U_WORD i=0; i<uwImageWidth; i++)
		VertProjection[i]=0;

	//Get the vertical profile of the block.
	for(i=0; i<uwImageWidth; i++)
		for(U_WORD j=0; j<uwImageHeight; j++)
			if(GetMonoPCXPixel(pImage, uwRowVolume, j, i)==0)
			{
				VertProjection[i]=1;
				break;  //To search the next column.
			}

	//Find the shadow region in the vertical profile.
	U_WORD uwStart=0, uwEnd=0;
	int iShadowLength=0;
	BOOL bFindStart=FALSE;
	i=0;
	do
	{
		//Skip the consecutive 0s.
		for(; i<uwImageWidth && VertProjection[i]==0; i++);

		if(i>=uwImageWidth) break;

		//Count the length of the consecutive 1s.
		int iRunLength=1;
		for(i++; i<uwImageWidth && VertProjection[i]; i++, iRunLength++);

		if(iRunLength>iNoiseRunLength)
		{
			iShadowLength+=iRunLength;

			if(bFindStart!=TRUE)
			{
				uwStart=i-iRunLength;
				bFindStart=TRUE;
			}

			uwEnd=i-1;
		}

		i++;
	} while(i<uwImageWidth);

	if(iShadowLength==0 || double(uwEnd-uwStart+1)/double(uwImageHeight)<
		SMALL_TEXT_BLOCK_ASPECT_MIN)
		return 0;

	int iTotalRunLength0=0,		 //To accumulate the total length of run of 0s.
		iNumRunLength0=0,		 //To count the appearance of run of 0s.
		iNumRowsHavingRunLength0=0;  //To count the number of rows where run of 0s appears.

	for(i=0; i<uwImageHeight; i++)
	{
		U_WORD j=uwStart;
//		BOOL bRunLength0Detected=FALSE;

		do
		{
			//Skip the consecutive 1s, i.e. the background (white) pixels.
			for(; j<uwEnd &&
				GetMonoPCXPixel(pImage, uwRowVolume, i, j); j++);

			//Check if reach the end of the row.
			if(j>=uwEnd) break;

			//A run of 0s is found. Count its length.
			int iRunLength0=1;
			for(j++; j<uwEnd &&
				GetMonoPCXPixel(pImage, uwRowVolume, i, j)==0; j++, iRunLength0++);

			if(iRunLength0>iNoiseRunLengthInSmallText)  //Take this one if it is not a noise.
			{
				iTotalRunLength0+=iRunLength0;
				iNumRunLength0++;
//				bRunLength0Detected=TRUE;
			}

			j++;  //Move to the next pixel.
		} while(j<uwEnd);

//		if(bRunLength0Detected) iNumRowsHavingRunLength0++;
	}

	//C language always uses "double" to calculate the floating point
	//  variables. If you use "float", they will be transformed into "double"
	//  for calculation and then the result is transformed back to "float".
	//  Besides, the floating point constants are also stored as "double".
	if(double(iTotalRunLength0)/double(iNumRunLength0)<=fRunLengthMax &&
	   double(iNumRunLength0)/double(iShadowLength)>=SMALL_TEXT_BLOCK_TRANSITION_COUNT_MIN &&
	   double(iNumRunLength0)/double(iShadowLength)<=SMALL_TEXT_BLOCK_TRANSITION_COUNT_MAX)
	{
/*
		//For debug.
		//Open the c:\temp\working\h.txt file for writing the data.
		FILE* pFile=fopen("c:\\temp\\working\\h.txt", "r+t");
		if(pFile)
			//Move the file pointer to the end of the file.
			fseek(pFile, long(-1), SEEK_END);
		else
			//Build a new file.
			pFile=fopen("c:\\temp\\working\\h.txt", "wt");

		//Write the data to the c:\temp\working\h.txt file.
		fprintf(pFile, "Transition count per unit width of "
						"the extracted text block:");
		fprintf(pFile, "%f\n", double(iNumRunLength0)/double(iShadowLength));

		//Close the file.
		fputc(0x0a, pFile);
		fputc(0x1a, pFile);
		fclose(pFile);
*/
		return 1;  //This is a text block.
	}
	else
		return 0;  //This is not a text block.
}

int LargeTextBlockVerification2(PBMPIMAGE pImage,
								U_WORD    uwImageWidth,
								U_WORD    uwImageHeight,
								U_WORD    uwRowVolume,
								float	 fRunLengthMin,
								float	 fRunLengthMax,
								int		 iNoiseRunLength)
{
	if(uwImageWidth>MAX_BLOCK_WIDTH)
	{
		AfxMessageBox("Block 的寬度超過 MAX_BLOCK_WIDTH 的設定值，\n"
					  "該 block 將被忽略。", MB_ICONINFORMATION);
		return 0;
	}

	char VertProjection[MAX_BLOCK_WIDTH];  //To store the vertical profile.
	for(U_WORD i=0; i<uwImageWidth; i++)
		VertProjection[i]=0;

	//Get the vertical profile of the block.
	for(i=0; i<uwImageWidth; i++)
		for(U_WORD j=0; j<uwImageHeight; j++)
			if(GetMonoPCXPixel(pImage, uwRowVolume, j, i)==0)
			{
				VertProjection[i]=1;
				break;  //To search the next column.
			}

	//Find the shadow region in the vertical profile.
	U_WORD uwStart=0, uwEnd=0;
	int iShadowLength=0;
	BOOL bFindStart=FALSE;
	i=0;
	do
	{
		//Skip the consecutive 0s.
		for(; i<uwImageWidth && VertProjection[i]==0; i++);

		if(i>=uwImageWidth) break;

		//Count the length of the consecutive 1s.
		int iRunLength=1;
		for(i++; i<uwImageWidth && VertProjection[i]; i++, iRunLength++);

		if(iRunLength>iNoiseRunLength)
		{
			iShadowLength+=iRunLength;

			if(bFindStart!=TRUE)
			{
				uwStart=i-iRunLength;
				bFindStart=TRUE;
			}

			uwEnd=i-1;
		}

		i++;
	} while(i<uwImageWidth);

	if(iShadowLength==0 || double(uwEnd-uwStart+1)/double(uwImageHeight)<
		LARGE_TEXT_BLOCK_ASPECT_MIN)
		return 0;

	int iTotalRunLength0=0,		 //To accumulate the total length of run of 0s.
		iNumRunLength0=0,		 //To count the appearance of run of 0s.
		iNumRowsHavingRunLength0=0;  //To count the number of rows where run of 0s appears.

	for(i=0; i<uwImageHeight; i++)
	{
		U_WORD j=uwStart;
//		BOOL bRunLength0Detected=FALSE;

		do
		{
			//Skip the consecutive 1s, i.e. the background (white) pixels.
			for(; j<uwEnd &&
				GetMonoPCXPixel(pImage, uwRowVolume, i, j); j++);

			//Check if reach the end of the row.
			if(j>=uwEnd) break;

			//A run of 0s is found. Count its length.
			int iRunLength0=1;
			for(j++; j<uwEnd &&
				GetMonoPCXPixel(pImage, uwRowVolume, i, j)==0; j++, iRunLength0++);

			if(iRunLength0>iNoiseRunLength)  //Take this one if it is not a noise.
			{
				iTotalRunLength0+=iRunLength0;
				iNumRunLength0++;
//				bRunLength0Detected=TRUE;
			}

			j++;  //Move to the next pixel.
		} while(j<uwEnd);

//		if(bRunLength0Detected) iNumRowsHavingRunLength0++;
	}

	//C language always uses "double" to calculate the floating point
	//  variables. If you use "float", they will be transformed into "double"
	//  for calculation and then the result is transformed back to "float".
	//  Besides, the floating point constants are also stored as "double".
	float fMeanRunLength=float(double(iTotalRunLength0)/double(iNumRunLength0));
	if(fMeanRunLength>=fRunLengthMin && fMeanRunLength<=fRunLengthMax &&
	   double(iNumRunLength0)/double(iShadowLength)>=LARGE_TEXT_BLOCK_TRANSITION_COUNT_MIN &&
	   double(iNumRunLength0)/double(iShadowLength)<=LARGE_TEXT_BLOCK_TRANSITION_COUNT_MAX)
	{
/*
		//For debug.
		//Open the c:\temp\working\h.txt file for writing the data.
		FILE* pFile=fopen("c:\\temp\\working\\h.txt", "r+t");
		if(pFile)
			//Move the file pointer to the end of the file.
			fseek(pFile, long(-1), SEEK_END);
		else
			//Build a new file.
			pFile=fopen("c:\\temp\\working\\h.txt", "wt");

		//Write the data to the c:\temp\working\h.txt file.
		fprintf(pFile, "Transition count per unit width "
						"of the extracted text blocks:");
		fprintf(pFile, "%f\n", double(iNumRunLength0)/double(iShadowLength));

		//Close the file.
		fputc(0x0a, pFile);
		fputc(0x1a, pFile);
		fclose(pFile);
*/
		return 1;  //This is a text block.
	}
	else
		return 0;  //This is not a text block.
}
