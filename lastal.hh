#ifndef __LASTAL_HH
#define __LASTAL_HH

#include "LastalArguments.hh"
#include "QualityPssmMaker.hh"
#include "OneQualityScoreMatrix.hh"
#include "TwoQualityScoreMatrix.hh"
#include "qualityScoreUtil.hh"
#include "LambdaCalculator.hh"
#include "GeneticCode.hh"
#include "SubsetSuffixArray.hh"
#include "Centroid.hh"
#include "GappedXdropAligner.hh"
#include "AlignmentPot.hh"
#include "Alignment.hh"
#include "SegmentPairPot.hh"
#include "SegmentPair.hh"
#include "ScoreMatrix.hh"
#include "Alphabet.hh"
#include "MultiSequence.hh"
#include "DiagonalTable.hh"
#include "GeneralizedAffineGapCosts.hh"
#include "gaplessXdrop.hh"
#include "gaplessPssmXdrop.hh"
#include "gaplessTwoQualityXdrop.hh"
#include "io.hh"
#include "stringify.hh"
#include "lastex.hh"
#include "LastexArguments.hh"


#include <iostream>
#include <fstream>
#include <stdexcept>
#include <ctime>
#include <cstdlib>  // EXIT_SUCCESS, EXIT_FAILURE
#include <string>
#include <vector>


#include <cstdlib>
#include <pthread.h>
#include <semaphore.h>


#ifdef MAC_SEM
  typedef *sem_t SEM_T;
  #define SEM_POST(x) sem_post(x)
  #define SEM_WAIT(x) sem_wait(X)
#else
  typedef sem_t SEM_T;
  #define SEM_POST(x) sem_post(&x)
  #define SEM_WAIT(x) sem_wait(&x)
#endif

#define ERR(x) throw std::runtime_error(x)
#define LOG(x) if( args.verbosity > 0 ) std::cerr << "lastal: " << x << '\n'

using namespace cbrc;

typedef MultiSequence::indexT indexT;
typedef unsigned long long countT;

namespace Phase{ 
  enum Enum{ gapless, gapped, final }; 
}

namespace {

  LastalArguments args;
  Alphabet alph;
  Alphabet queryAlph;  // for translated alignment
  GeneticCode geneticCode;
  const unsigned maxNumOfIndexes = 16;
  SubsetSuffixArray suffixArrays[maxNumOfIndexes];
  ScoreMatrix scoreMatrix;
  GeneralizedAffineGapCosts gapCosts;
  LambdaCalculator lambdaCalculator;
  std::vector< std::vector<countT> > matchCounts;  // used if outputType == 0
  OneQualityScoreMatrix oneQualityScoreMatrix;
  OneQualityScoreMatrix oneQualityScoreMatrixMasked;
  OneQualityExpMatrix oneQualityExpMatrix;
  QualityPssmMaker qualityPssmMaker;
  sequenceFormat::Enum referenceFormat;  // defaults to 0
  TwoQualityScoreMatrix twoQualityScoreMatrix;
  TwoQualityScoreMatrix twoQualityScoreMatrixMasked;
  int minScoreGapless;
  int isCaseSensitiveSeeds = -1;  // initialize it to an "error" value
  unsigned numOfIndexes = 1;  // assume this value, if unspecified
}

struct threadData{

  Alphabet alph;
  Alphabet queryAlph;  // for translated alignment
  GeneticCode geneticCode;
  SubsetSuffixArray suffixArrays[16];
  GappedXdropAligner gappedXdropAligner;
  Centroid *centroid;
  MultiSequence query;  // sequence that hasn't been indexed by lastdb
  MultiSequence text;  // sequence that has been indexed by lastdb
  std::vector< std::vector<countT> > matchCounts;  // used if outputType == 0
  OneQualityScoreMatrix oneQualityScoreMatrix;
  OneQualityScoreMatrix oneQualityScoreMatrixMasked;
  OneQualityExpMatrix oneQualityExpMatrix;
  QualityPssmMaker qualityPssmMaker;
  TwoQualityScoreMatrix twoQualityScoreMatrix;
  TwoQualityScoreMatrix twoQualityScoreMatrixMasked;
  std::vector< std::string > *outputVector;

  //void alignGapless( SegmentPairPot& gaplessAlns, char strand, std::ostream& out);
  void alignGapless( SegmentPairPot& gaplessAlns, char strand );
  void alignGapped( AlignmentPot& gappedAlns, SegmentPairPot& gaplessAlns, Phase::Enum phase );
  //void alignFinish( const AlignmentPot& gappedAlns, char strand, std::ostream& out );
  void alignFinish( const AlignmentPot& gappedAlns, char strand );
  void makeQualityPssm( bool isApplyMasking );
  //void scan( char strand, std::ostream& out );
  void scan( char strand );
  //void translateAndScan( char strand, std::ostream& out);
  void translateAndScan( char strand );
  void reverseComplementPssm();
  void reverseComplementQuery();
  //void scanAllVolumes( unsigned volumes, std::ostream& out );
  void scanAllVolumes( unsigned volumes );
  void prepareThreadData();
  void readIndex( const std::string& baseName, indexT seqCount );
  void readVolume( unsigned volumeNumber );
  void countMatches( char strand );
  //void writeCounts( std::ostream& out );
  void writeCounts();
  std::istream& appendFromFasta( std::istream& in );
  void callReinit();

};

struct Dispatcher: public threadData{

  const uchar* a;  // the reference sequence
  const uchar* b;  // the query sequence
  const uchar* i;  // the reference quality data
  const uchar* j;  // the query quality data
  const ScoreMatrixRow* p;  // the query PSSM
  const ScoreMatrixRow* m;  // the score matrix
  const TwoQualityScoreMatrix& t;
  int d;  // the maximum score drop
  int z;

  Dispatcher( Phase::Enum e ) :
    a( text.seqReader() ),
    b( query.seqReader() ),
    i( text.qualityReader() ),
    j( query.qualityReader() ),
    p( query.pssmReader() ),
    m( (e < args.maskLowercase) ?
        scoreMatrix.caseSensitive : scoreMatrix.caseInsensitive ),
    t( (e < args.maskLowercase) ?
        twoQualityScoreMatrixMasked : twoQualityScoreMatrix ),
    d( (e == Phase::gapless) ? args.maxDropGapless :
        (e == Phase::gapped ) ? args.maxDropGapped : args.maxDropFinal ),
    z( (args.inputFormat == sequenceFormat::fasta) ? 0 :
        (referenceFormat  == sequenceFormat::fasta) ? 1 : 2 ){}

  //void shrinkToLongestIdenticalRun( SegmentPair& sp, const Dispatcher& dis );
  void shrinkToLongestIdenticalRun( SegmentPair& sp);

  int forwardGaplessScore( indexT x, indexT y ) const{
    if( z==0 ) return forwardGaplessXdropScore( a+x, b+y, m, d );
    if( z==1 ) return forwardGaplessPssmXdropScore( a+x, p+y, d );
    return forwardGaplessTwoQualityXdropScore( a+x, i+x, b+y, j+y, t, d );
  }

  int reverseGaplessScore( indexT x, indexT y ) const{
    if( z==0 ) return reverseGaplessXdropScore( a+x, b+y, m, d );
    if( z==1 ) return reverseGaplessPssmXdropScore( a+x, p+y, d );
    return reverseGaplessTwoQualityXdropScore( a+x, i+x, b+y, j+y, t, d );
  }

  indexT forwardGaplessEnd( indexT x, indexT y, int s ) const{
    if( z==0 ) return forwardGaplessXdropEnd( a+x, b+y, m, s ) - a;
    if( z==1 ) return forwardGaplessPssmXdropEnd( a+x, p+y, s ) - a;
    return forwardGaplessTwoQualityXdropEnd( a+x, i+x, b+y, j+y, t, s ) - a;
  }

  indexT reverseGaplessEnd( indexT x, indexT y, int s ) const{
    if( z==0 ) return reverseGaplessXdropEnd( a+x, b+y, m, s ) - a;
    if( z==1 ) return reverseGaplessPssmXdropEnd( a+x, p+y, s ) - a;
    return reverseGaplessTwoQualityXdropEnd( a+x, i+x, b+y, j+y, t, s ) - a;
  }

  bool isOptimalGapless( indexT x, indexT e, indexT y ) const{
    if( z==0 ) return isOptimalGaplessXdrop( a+x, a+e, b+y, m, d );
    if( z==1 ) return isOptimalGaplessPssmXdrop( a+x, a+e, p+y, d );
    return isOptimalGaplessTwoQualityXdrop( a+x, a+e, i+x, b+y, j+y, t, d );
  }

  int gaplessScore( indexT x, indexT e, indexT y ) const{
    if( z==0 ) return gaplessAlignmentScore( a+x, a+e, b+y, m );
    if( z==1 ) return gaplessPssmAlignmentScore( a+x, a+e, p+y );
    return gaplessTwoQualityAlignmentScore( a+x, a+e, i+x, b+y, j+y, t );
  }
};

void makeScoreMatrix( const std::string& matrixFile) ;
void makeQualityScorers();
void calculateScoreStatistics();
void readOuterPrj( const std::string& fileName, unsigned& volumes, indexT& minSeedLimit,
    countT& refSequences, countT& refLetters );
void readInnerPrj( const std::string& fileName, indexT& seqCount, indexT& seqLen );
void writeHeader( countT refSequences, countT refLetters, std::ostream& out );
void initializeEvalueCalulator(const std::string dbPrjFile, std::string dbfilePrj);

void writerFunction( std::ostream& out );
void* threadFunction( void *args ); 
void lastal( int argc, char** argv );

#endif
