/* methdiff: Computes probability that individual CpGs have higher
 *           methylation in file 1 than 2
 *
 * Copyright (C) 2011 University of Southern California
 *                    Andrew D Smith
 *
 * Author: Andrew D. Smith
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <gsl/gsl_sf_gamma.h>

#include <cmath>
#include <fstream>
#include <utility>

#include "smithlab_utils.hpp"
#include "smithlab_os.hpp"
#include "GenomicRegion.hpp"
#include "OptionParser.hpp"
#include "MethpipeFiles.hpp"


using std::string;
using std::vector;
using std::cout;
using std::endl;
using std::cerr;
using std::pair;

using std::ostream_iterator;
using std::ofstream;

static inline double
log_sum_log(const double p, const double q) {
  if (p == 0) {return q;}
  else if (q == 0) {return p;}
  const double larger = (p > q) ? p : q;
  const double smaller = (p > q) ? q : p;
  return larger + log(1.0 + exp(smaller - larger));
}

static void
get_meth_unmeth(const GenomicRegion &cpg, size_t &meth, size_t &unmeth) {
  const double prob = cpg.get_score();
  const string name(cpg.get_name());
  const size_t n_reads = atoi(name.substr(name.find_first_of(":") + 1).c_str());
  meth = static_cast<size_t>(prob*n_reads);
  unmeth = n_reads - meth;
}

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

static double
log_hyper_g_greater(size_t meth_a, size_t unmeth_a, 
                    size_t meth_b, size_t unmeth_b, size_t k) {
  return  gsl_sf_lnchoose(meth_b + unmeth_b - 1, k) + 
    gsl_sf_lnchoose(meth_a + unmeth_a - 1, meth_a + meth_b - 1 - k) -
    gsl_sf_lnchoose(meth_a + unmeth_a + meth_b + unmeth_b - 2, 
		    meth_a + meth_b - 1);
}
  

static double
test_greater_population(size_t meth_a, size_t unmeth_a, 
			size_t meth_b, size_t unmeth_b) {
  double p = 0;
  
  for (size_t k = (meth_b > unmeth_a) ? meth_b - unmeth_a : 0; k < meth_b; ++k)
    p = log_sum_log(p, log_hyper_g_greater(meth_a, unmeth_a, meth_b, unmeth_b, k));
  return exp(p);
}


////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////


int
main(int argc, const char **argv) {

  try {

    string outfile;
    size_t pseudocount = 1;
    
    // run mode flags
    bool OUTPUT_ALL_LOCI = false;
    bool VERBOSE = false;
    
    /****************** COMMAND LINE OPTIONS ********************/
    OptionParser opt_parse(strip_path(argv[0]),
			   "Computes probability that "
			   "individual CpGs have higher methylation in "
			   "file A than B",
			   "<cpgs-BED-file-A> <cpgs-BED-file-B>");
    opt_parse.add_opt("pseudo", 'p', "pseudocount (default: 1)", 
		      false, pseudocount);
    opt_parse.add_opt("all-loci", 'A',
                      "output all loci ()including low coverage ones)", 
                      false, OUTPUT_ALL_LOCI);
    opt_parse.add_opt("out", 'o', "output file (BED format)", 
		      false, outfile);
    opt_parse.add_opt("verbose", 'v', "print more run info", false, VERBOSE);
    vector<string> leftover_args;
    opt_parse.parse(argc, argv, leftover_args);
    if (argc == 1 || opt_parse.help_requested()) {
      cerr << opt_parse.help_message() << endl
	   << opt_parse.about_message() << endl;
      return EXIT_SUCCESS;
    }
    if (opt_parse.about_requested()) {
      cerr << opt_parse.about_message() << endl;
      return EXIT_SUCCESS;
    }
    if (opt_parse.option_missing()) {
      cerr << opt_parse.option_missing_message() << endl;
      return EXIT_SUCCESS;
    }
    if (leftover_args.size() != 2) {
      cerr << opt_parse.help_message() << endl;
      return EXIT_SUCCESS;
    }
    const string cpgs_file_a = leftover_args[0];
    const string cpgs_file_b = leftover_args[1];
    /****************** END COMMAND LINE OPTIONS *****************/
    if (methpipe::is_methpipe_file_single(cpgs_file_a)
        && methpipe::is_methpipe_file_single(cpgs_file_b)) {

      if (VERBOSE)
        cerr << "[READING CPGS]";
      vector<GenomicRegion> cpgs_a;
      vector<pair<double, double> > meth_unmeth_a;
      vector<size_t> reads_a;
      methpipe::load_cpgs(cpgs_file_a, cpgs_a, meth_unmeth_a, reads_a);
      if (VERBOSE) 
        cerr << "[READ=" + strip_path(cpgs_file_a) + "]" << endl;

      if (VERBOSE)
        cerr << "[READING CPGS]";
      vector<GenomicRegion> cpgs_b;
      vector<pair<double, double> > meth_unmeth_b;
      vector<size_t> reads_b;
      methpipe::load_cpgs(cpgs_file_b, cpgs_b, meth_unmeth_b, reads_b);
      if (VERBOSE) 
        cerr << "[READ=" + strip_path(cpgs_file_b) + "]" << endl;

      if (VERBOSE)
        cerr << "CPG COUNT A: " << cpgs_a.size() << endl
             << "CPG COUNT B: " << cpgs_b.size() << endl;

      std::ofstream out(outfile.empty() ? "/dev/stdout" : outfile.c_str());

      size_t j = 0;
      for (size_t i = 0; i < cpgs_a.size(); ++i) {
        const size_t meth_a(static_cast<size_t>(meth_unmeth_a[i].first)),
          unmeth_a(static_cast<size_t>(meth_unmeth_a[i].first));

        if (VERBOSE && (i == 0 || !cpgs_a[i - 1].same_chrom(cpgs_a[i])))
          cerr << "[PROCESSING] " << cpgs_a[i].get_chrom() << endl;
      
        while (j < cpgs_b.size() && cpgs_b[j] < cpgs_a[i]) ++j;
      
        if (cpgs_a[i].same_chrom(cpgs_b[j]) && 
            cpgs_a[i].get_start() == cpgs_b[j].get_start()) {
	
          const size_t meth_b(static_cast<size_t>(meth_unmeth_b[j].first)),
            unmeth_b(static_cast<size_t>(meth_unmeth_b[j].first));
	
          if (meth_a + unmeth_a > 0.0 && meth_b + unmeth_b > 0.0) {
            cpgs_a[i].set_name("CpG:" + 
                               toa(meth_a) + ":" + toa(unmeth_a) + ":" +
                               toa(meth_b) + ":" + toa(unmeth_b));
            cpgs_a[i].set_score(test_greater_population(meth_b + pseudocount,
                                                        unmeth_b + pseudocount, 
                                                        meth_a + pseudocount, 
                                                        unmeth_a + pseudocount));
            out << cpgs_a[i] << endl;
          } else if (OUTPUT_ALL_LOCI) {
            cpgs_a[i].set_name("CpG:" + 
                               toa(meth_a) + ":" + toa(unmeth_a) + ":" +
                               toa(meth_b) + ":" + toa(unmeth_b));
            cpgs_a[i].set_score(test_greater_population(meth_b + pseudocount,
                                                        unmeth_b + pseudocount, 
                                                        meth_a + pseudocount, 
                                                        unmeth_a + pseudocount));
            out << cpgs_a[i] << endl;
          }
        }
      }
    } else {
      if (VERBOSE)
        cerr << "[READING CPGS]";
      vector<GenomicRegion> cpgs_a, cpgs_b;
      ReadBEDFile(cpgs_file_a, cpgs_a);
      if (!check_sorted(cpgs_a))
        throw SMITHLABException("CpGs not sorted in file \"" + cpgs_file_a + "\"");
      if (VERBOSE) 
        cerr << "[READ=" + strip_path(cpgs_file_a) + "]" << endl;
    
      ReadBEDFile(cpgs_file_b, cpgs_b);
      if (!check_sorted(cpgs_b))
        throw SMITHLABException("CpGs not sorted in file \"" + cpgs_file_b + "\"");
      if (VERBOSE) 
        cerr << "[READ=" + strip_path(cpgs_file_b) + "]" << endl;

      if (VERBOSE)
        cerr << "CPG COUNT A: " << cpgs_a.size() << endl
             << "CPG COUNT B: " << cpgs_b.size() << endl;
    
      std::ofstream out(outfile.empty() ? "/dev/stdout" : outfile.c_str());

      size_t j = 0;
      for (size_t i = 0; i < cpgs_a.size(); ++i) {
        if (VERBOSE && (i == 0 || !cpgs_a[i - 1].same_chrom(cpgs_a[i])))
          cerr << "[PROCESSING] " << cpgs_a[i].get_chrom() << endl;
      
        while (j < cpgs_b.size() && cpgs_b[j] < cpgs_a[i]) ++j;
      
        if (cpgs_a[i].same_chrom(cpgs_b[j]) && 
            cpgs_a[i].get_start() == cpgs_b[j].get_start()) {
	
          size_t meth_a = 0, unmeth_a = 0;
          get_meth_unmeth(cpgs_a[i], meth_a, unmeth_a);
	
          size_t meth_b = 0, unmeth_b = 0;
          get_meth_unmeth(cpgs_b[j], meth_b, unmeth_b);
	
          if (meth_a + unmeth_a > 0.0 && meth_b + unmeth_b > 0.0) {
            cpgs_a[i].set_name("CpG:" + 
                               toa(meth_a) + ":" + toa(unmeth_a) + ":" +
                               toa(meth_b) + ":" + toa(unmeth_b));
            cpgs_a[i].set_score(test_greater_population(meth_b + pseudocount,
                                                        unmeth_b + pseudocount, 
                                                        meth_a + pseudocount, 
                                                        unmeth_a + pseudocount));
            out << cpgs_a[i] << endl;
          } else if (OUTPUT_ALL_LOCI) {
            cpgs_a[i].set_name("CpG:" + 
                               toa(meth_a) + ":" + toa(unmeth_a) + ":" +
                               toa(meth_b) + ":" + toa(unmeth_b));
            cpgs_a[i].set_score(test_greater_population(meth_b + pseudocount,
                                                        unmeth_b + pseudocount, 
                                                        meth_a + pseudocount, 
                                                        unmeth_a + pseudocount));
            out << cpgs_a[i] << endl;
          }
        }
      }
    }
  }
  catch (SMITHLABException &e) {
    cerr << "ERROR:\t" << e.what() << endl;
    return EXIT_FAILURE;
  }
  catch (std::bad_alloc &ba) {
    cerr << "ERROR: could not allocate memory" << endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
