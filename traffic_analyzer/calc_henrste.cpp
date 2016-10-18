#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <unistd.h>
#include <fstream>
#include <vector>
#include <math.h>
#include <sstream>
#include <string>
#include <map>
#include <unistd.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <string.h>
#include <algorithm>

#define NRSAMPLES 250
#define PDF_BINS 50
#define MAX_QS 2048

#define percentile(p, n) (ceil(float(p)/100*float(n)))

struct Parameters {
    double rtt_d;
    double rtt_r;
    std::string folder;
    uint32_t n_dctcp;
    uint32_t n_reno;
    std::string fairness;
    int nbrf;
    double link;

    Parameters() {
        rtt_d = 0;
        rtt_r = 0;
        folder = "";
        n_dctcp = 0;
        n_reno = 0;
        fairness = "";
        nbrf = 0;
        link = 0;
    }
};

class Statistics {
  public:
    Statistics() {
        calculated_variance = false;
        calculated_coeffVar = false;
        _variance = NAN;
        _average = NAN;
        _coeffVar = NAN;
        _samples = NULL;
    }

    void samples(std::vector<double> *new_samples) {
        _samples = new_samples;
        std::sort(_samples->begin(), _samples->end());
    }

    std::vector<double> *samples() {
        return _samples;
    }

    double p(double p) {
        if (_samples != NULL && _samples->size() > 0) {
            return _samples->at(percentile(p, _samples->size()) - 1);
        }

        return NAN;
    }

    double variance() {
        if (_samples != NULL && !calculated_variance) {
            calculate_variance();
        }

        return _variance;
    }

    double average() {
        if (_samples != NULL && !calculated_variance) {
            calculate_variance();
        }

        return _average;
    }

    double coeffVar() {
        if (_samples != NULL && !calculated_coeffVar) {
            calculate_coeffVar();
        }

        return _coeffVar;
    }

  private:
    bool calculated_variance;
    bool calculated_coeffVar;
    double _variance;
    double _average;
    double _coeffVar;
    std::vector<double> *_samples;

    void calculate_coeffVar() {
        if (variance() > 0 && average() > 0) {
            _coeffVar = sqrt(variance()) / average();
        } else {
            _coeffVar = 0;
        }

        calculated_coeffVar = true;
    }

    void calculate_variance() {
        double tot = 0;
        long double sumsq = 0;
        uint32_t n_samples = _samples->size();

        for (double val: *_samples) {
            tot += val;
            sumsq += (long double) val * val;
        }

        _variance = NAN;
        if (n_samples > 1) {
            _variance = ((double(n_samples) * sumsq) - (tot * tot)) / (double(n_samples) * double(n_samples - 1));
        }

        _average = tot / n_samples;
        calculated_variance = true;
    }
};

struct Results {
    Statistics *rate_ecn;
    Statistics *rate_nonecn;
    Statistics *win_ecn;
    Statistics *win_nonecn;
    Statistics *qs_ecn;
    Statistics *qs_nonecn;
    Statistics *drops_qs_ecn;
    Statistics *drops_qs_nonecn;
    Statistics *marks_ecn;
    Statistics *util; // utilization

    double rr_static;
    double wr_static;

    double ecn_avg;
    double nonecn_avg;

    uint64_t tot_sent_dropped_ecn;
    uint64_t tot_sent_dropped_nonecn;

    Results() {
        rate_ecn = new Statistics();
        rate_nonecn = new Statistics();
        win_ecn = new Statistics();
        win_nonecn = new Statistics();
        qs_ecn = new Statistics();
        qs_nonecn = new Statistics();
        drops_qs_ecn = new Statistics();
        drops_qs_nonecn = new Statistics();
        marks_ecn = new Statistics();
        util = new Statistics();

        rr_static = NAN;
        wr_static = NAN;

        ecn_avg = NAN;
        nonecn_avg = NAN;

        tot_sent_dropped_ecn = 0;
        tot_sent_dropped_nonecn = 0;
    }
};

struct Parameters *params = new Parameters();
struct Results *res = new Results();

void usage(int argc, char* argv[]) {
    printf("Usage: %s <folder> <e=rate_equal|d=dc_unequal> <nbr of flows per row/col> <link b/s> <rtt_d> <rtt_r> <nr dctcp flows> <nr reno flows>\n", argv[0]);
    exit(1);
}

std::ofstream* openFileW(std::string filename) {
    std::string filename_out = params->folder + "/" + filename;
    std::ofstream *f;
    f = new std::ofstream(filename_out.c_str());

    if (!f->is_open()) {
        std::cerr << "error opening file: " << filename_out << std::endl;
        exit(1);
    }

    return f;
}

void writeToFile(std::string filename, std::string data) {
    std::ofstream *fs = openFileW(filename);
    *fs << data;
    fs->close();
}

void dmPDF(Statistics *drops_ecn, Statistics *drops_nonecn, Statistics *marks_ecn, int i) {
    std::ofstream *f_decn_pdf = openFileW("d_pf_ecn_pdf");
    std::ofstream *f_mecn_pdf = openFileW("m_pf_ecn_pdf");
    std::ofstream *f_dnonecn_pdf = openFileW("d_pf_nonecn_pdf");

    std::vector<double> *samples_drops_ecn = drops_ecn->samples();
    std::vector<double> *samples_drops_nonecn = drops_nonecn->samples();
    std::vector<double> *samples_marks_ecn = marks_ecn->samples();

    uint32_t decn_pdf[PDF_BINS];
    uint32_t dnonecn_pdf[PDF_BINS];
    uint32_t mecn_pdf[PDF_BINS];
    bzero(decn_pdf, sizeof(uint32_t)*PDF_BINS);
    bzero(dnonecn_pdf, sizeof(uint32_t)*PDF_BINS);
    bzero(mecn_pdf, sizeof(uint32_t)*PDF_BINS);

    uint32_t max = 0;

    if (samples_drops_ecn->back() > max)
        max = samples_drops_ecn->back();
    if (samples_drops_nonecn->back() > max)
        max = samples_drops_nonecn->back();

    uint32_t binsize = max/PDF_BINS;
    uint32_t b;

    for (double val: *samples_drops_ecn) {
        b = val / binsize;
        if (b >= PDF_BINS)
            b = PDF_BINS - 1;
        decn_pdf[b]++;
    }

    for (double val: *samples_drops_nonecn) {
        b = val / binsize;
        if (b >= PDF_BINS)
            b = PDF_BINS - 1;
        dnonecn_pdf[b]++;
    }

    if (samples_marks_ecn->back() > max)
        max = samples_marks_ecn->back();

    binsize = max/PDF_BINS;
    for (double val: *samples_marks_ecn) {
        b = val / binsize;
        if (b >= PDF_BINS)
            b = PDF_BINS - 1;
        mecn_pdf[b]++;
    }

    for (int i = 0; i < PDF_BINS; ++i) {
        *f_decn_pdf << i << " " << decn_pdf[i] << std::endl;
        *f_mecn_pdf << i << " " << mecn_pdf[i] << std::endl;
        *f_dnonecn_pdf << i << " " << dnonecn_pdf[i] << std::endl;
    }

    f_decn_pdf->close();
    f_mecn_pdf->close();
    f_dnonecn_pdf->close();
}

void rPDF(Statistics *rate_ecn, Statistics *rate_nonecn, char fairness, int dctcp, int reno, int nbrf) {
    std::ofstream *f_recn_pdf = openFileW("r_pf_ecn_pdf");
    std::ofstream *f_rnonecn_pdf = openFileW("r_pf_nonecn_pdf");
    uint32_t recn_pdf[PDF_BINS];
    uint32_t rnonecn_pdf[PDF_BINS];
    bzero(recn_pdf, sizeof(uint32_t)*PDF_BINS);
    bzero(rnonecn_pdf, sizeof(uint32_t)*PDF_BINS);

    std::vector<double> *samples_rate_ecn = rate_ecn->samples();
    std::vector<double> *samples_rate_nonecn = rate_nonecn->samples();

    uint32_t max = fairness == 'e' ? dctcp + reno : (dctcp ? dctcp : reno);
    if (max == 0)
        max = 1;

    max = 10000000/max/nbrf;

    uint32_t binsize = max/PDF_BINS;
    uint32_t b;

    for (double val: *samples_rate_ecn) {
        b = val / binsize;
        if (b >= PDF_BINS)
            b = PDF_BINS - 1;
        recn_pdf[b]++;
    }

    for (double val: *samples_rate_nonecn) {
        b = val / binsize;
        if (b >= PDF_BINS)
            b = PDF_BINS - 1;
        rnonecn_pdf[b]++;
    }

    for (int i = 0; i < PDF_BINS; ++i) {
        *f_recn_pdf << i << " " << recn_pdf[i] << std::endl;
        *f_rnonecn_pdf << i << " " << rnonecn_pdf[i] << std::endl;
    }

    f_recn_pdf->close();
    f_rnonecn_pdf->close();
}

void readFileMarks(std::string filename_marks, Statistics *stats, std::string filename_tot) {
    std::ifstream infile_marks(filename_marks.c_str());
    std::ifstream infile_tot(filename_tot.c_str());

    double marks;
    double tot_packets;
    std::vector<double> *samples = new std::vector<double>();

    for (int s = 0; s < NRSAMPLES; ++s) {
        for (int colnr = 0; colnr < 3; ++colnr) {
            if (infile_marks.eof() || infile_tot.eof())
                break;

            infile_marks >> marks;

            if (colnr == 2) {
                infile_tot >> tot_packets;
                double marks_perc = 0;

                if (tot_packets > 0) {
                    marks_perc = marks * 100 / tot_packets;
                }

                samples->push_back(marks_perc);
            }
        }
    }

    infile_marks.close();
    infile_tot.close();
    stats->samples(samples);
}

void readFileDrops(std::string filename_drops, Statistics *stats, std::string filename_tot) {
    std::ifstream infile_drops(filename_drops.c_str());
    std::ifstream infile_tot(filename_tot.c_str());

    double drops;
    double tot_packets;
    std::vector<double> *samples = new std::vector<double>();

    for (int s = 0; s < NRSAMPLES; ++s) {
        for (int colnr = 0; colnr < 3; ++colnr) {
            if (infile_drops.eof() || infile_tot.eof())
                break;

            infile_drops >> drops;

            if (colnr == 2) {
                infile_tot >> tot_packets;
                double drops_perc = 0;

                if (tot_packets+drops > 0)
                    drops_perc = drops*100/(tot_packets+drops);

                samples->push_back(drops_perc);

                if (drops_perc > 100)
                    std::cout << "too large drops perc: " << drops_perc << std::endl;
            }
        }
    }

    infile_drops.close();
    infile_tot.close();
    stats->samples(samples);
}

void readFileRate(std::string filename, int nrflows, Statistics *stats_rate, Statistics *stats_win, double avg_qs, double rtt) {
    std::ifstream infile(filename.c_str());
    double rate;

    std::vector<double> *samples_rate = new std::vector<double>();
    std::vector<double> *samples_win = new std::vector<double>();

    for (int s = 0; s < NRSAMPLES; ++s) {
        for(std::string line; getline(infile, line);) {
            std::istringstream iss(line);
            int colnr = 0;

            while (iss >> rate) {
                if (colnr++ >= 2) {
                    double win = 0;
                    if (avg_qs != 0) {
                        win = rate*(avg_qs+rtt)/1000;
                    }

                    samples_rate->push_back(rate);
                    samples_win->push_back(win);
                }
            }

            for (int i = colnr; i < nrflows; ++i) {
                samples_rate->push_back(0);
                samples_win->push_back(0);
            }
        }
    }

    infile.close();
    stats_rate->samples(samples_rate);
    stats_win->samples(samples_win);
}

void getSamplesUtilization() {
    double link_bytes_ps = (double) params->link / 8;

    std::string filename_ecn = params->folder + "/r_tot_ecn";
    std::string filename_nonecn = params->folder + "/r_tot_nonecn";

    std::ifstream infile_ecn(filename_ecn.c_str());
    std::ifstream infile_nonecn(filename_nonecn.c_str());

    std::vector<double> *samples = new std::vector<double>();
    double rate_ecn;
    double rate_nonecn;
    double util;

    // each line consists of three numbers, and we only want the last number
    for (int s = 0; s < NRSAMPLES*3; ++s) {
        if (infile_ecn.eof() || infile_nonecn.eof()) {
            break;
        }

        infile_ecn >> rate_ecn;
        infile_nonecn >> rate_nonecn;

        if ((s+1)%3 == 0) {
            util = (rate_ecn+rate_nonecn)*100/link_bytes_ps;
            samples->push_back(util);
        }
    }

    infile_ecn.close();
    infile_nonecn.close();
    res->util->samples(samples);
}

void readFileQS(std::string filename, Statistics *stats, uint64_t *tot_sent_dropped) {
    std::ifstream infile(filename.c_str());
    if (!infile.is_open()) {
        std::cerr << "error opening file: " << filename << std::endl;
        return;
    }

    double tot_sent = 0;
    double tot_dropped = 0;
    double nrpackets, drops;
    uint32_t qsize = 0;
    std::vector<double> *samples = new std::vector<double>();

    while (!infile.eof() && qsize < MAX_QS) {
        infile >> nrpackets;
        infile >> drops;

        for (int i = 0; i < nrpackets; ++i)
            samples->push_back(qsize);

        tot_sent += nrpackets;
        tot_dropped += drops;
        qsize++;
    }

    infile.close();
    stats->samples(samples);
    *tot_sent_dropped = (uint64_t)(tot_sent + tot_dropped);
}

void getSamplesRateMarksDrops() {
    readFileRate(params->folder + "/r_tot_ecn", params->n_dctcp, res->rate_ecn, res->win_ecn, res->qs_ecn->average(), params->rtt_d);
    readFileMarks(params->folder + "/m_tot_ecn", res->marks_ecn, params->folder + "/tot_packets_ecn");
    readFileDrops(params->folder + "/d_tot_ecn", res->drops_qs_ecn, params->folder + "/tot_packets_ecn");
    readFileRate(params->folder + "/r_tot_nonecn", params->n_reno, res->rate_nonecn, res->win_nonecn, res->qs_nonecn->average(), params->rtt_r);
    readFileDrops(params->folder + "/d_tot_nonecn", res->drops_qs_nonecn, params->folder + "/tot_packets_nonecn");
}

void getSamplesQS() {
    readFileQS(params->folder + "/qs_drops_ecn_pdf", res->qs_ecn, &res->tot_sent_dropped_ecn);
    readFileQS(params->folder + "/qs_drops_nonecn_pdf", res->qs_nonecn, &res->tot_sent_dropped_nonecn);
}

void loadParameters(int argc, char **argv) {
    if (argc < 9) {
        usage(argc, argv);
    }

    params->folder = argv[1];
    params->fairness = argv[2];
    params->nbrf = atoi(argv[3]);
    params->link = atoi(argv[4]);
    params->rtt_d = (double) atoi(argv[5]);
    params->rtt_r = (double) atoi(argv[6]);
    params->n_dctcp = atoi(argv[7]);
    params->n_reno = atoi(argv[8]);

    if (params->fairness.length() != 1) {
        usage(argc, argv);
    }
}

int main(int argc, char **argv) {
    loadParameters(argc, argv);

    getSamplesQS();
    getSamplesRateMarksDrops();
    getSamplesUtilization();

    if (params->n_dctcp > 0) {
        res->rr_static = res->rate_nonecn->average() / res->rate_ecn->average();
        res->wr_static = res->win_nonecn->average() / res->win_ecn->average();
    }

    //rPDF(res->rate_ecn, res->rate_nonecn, params->fairness[0], params->n_dctcp, params->n_reno, params->nbrf);
    //dmPDF(res->drops_qs_ecn, res->drops_qs_nonecn, res->marks_ecn, i);

    if (res->drops_qs_nonecn->p(99) > 100) {
        std::cerr << "too high drops p99: " << res->drops_qs_nonecn->p(99) << std::endl;
        exit(1);
    }

    std::stringstream out;

    out << res->rate_ecn->average() << std::endl;
    writeToFile("avgrate_ecn", out.str()); out.str("");

    out << res->rate_nonecn->average() << std::endl;
    writeToFile("avgrate_nonecn", out.str()); out.str("");

    out << "s" << params->n_dctcp << " " << res->qs_ecn->average() << " " << res->qs_ecn->p(99) << " " << res->qs_ecn->p(1) << " " << res->qs_ecn->p(25) << " " << res->qs_ecn->p(75) << " " << (res->rate_ecn->coeffVar()*res->qs_ecn->average()) << std::endl;
    writeToFile("avg99pqs_ecn_2d", out.str()); out.str("");

    out << "s" << params->n_reno <<  " " << res->qs_nonecn->average() << " " << res->qs_nonecn->p(99) << " " << res->qs_nonecn->p(1) << " " << res->qs_nonecn->p(25) << " " << res->qs_nonecn->p(75) << " " << (res->rate_nonecn->coeffVar()*res->qs_nonecn->average()) << std::endl;
    writeToFile("avg99pqs_nonecn_2d", out.str()); out.str("");

    out << "s" << params->n_dctcp <<  " " << res->rate_ecn->average() << " " << res->rate_ecn->p(99) << " " << res->rate_ecn->p(1) << " " <<(res->rate_ecn->coeffVar()*res->rate_ecn->average()) << std::endl;
    writeToFile("avgstddevrate_ecn_2d", out.str()); out.str("");

    out << "s" << params->n_reno << " " << res->rate_nonecn->average() << " " << res->rate_nonecn->p(99) << " " << res->rate_nonecn->p(1) << " " << (res->rate_ecn->coeffVar()*res->rate_nonecn->average()) << std::endl;
    writeToFile("avgstddevrate_nonecn_2d", out.str()); out.str("");

    out << "s" << params->n_dctcp <<  " " << res->win_ecn->average() << " " << res->win_ecn->p(99) << " " << res->win_ecn->p(1) << " " <<(res->win_ecn->coeffVar()*res->win_ecn->average()) << std::endl;
    writeToFile("avgstddevwin_ecn_2d", out.str()); out.str("");

    out << "s" << params->n_reno <<  " " << res->win_nonecn->average() << " " << res->win_nonecn->p(99) << " " << res->win_nonecn->p(1) << " " <<(res->win_nonecn->coeffVar()*res->win_nonecn->average()) << std::endl;
    writeToFile("avgstddevwin_nonecn_2d", out.str()); out.str("");

    out << "s" << params->n_dctcp <<  " " << res->drops_qs_ecn->average() << " " << res->drops_qs_ecn->p(99) << " " << res->drops_qs_ecn->p(1) << " " << sqrt(res->drops_qs_ecn->variance()) << std::endl;
    writeToFile("avg99pdrop_ecn_2d", out.str()); out.str("");

    out << "s" << params->n_reno << " " << res->drops_qs_nonecn->average() << " " << res->drops_qs_nonecn->p(99) << " " << res->drops_qs_nonecn->p(1) << " " << sqrt(res->drops_qs_nonecn->variance()) << std::endl;
    writeToFile("avg99pdrop_nonecn_2d", out.str()); out.str("");

    out << "s" << params->n_dctcp <<  " " << res->marks_ecn->average() << " " << res->marks_ecn->p(99) << " " << res->marks_ecn->p(1) << " " << sqrt(res->marks_ecn->variance()) << std::endl;
    writeToFile("avg99pmark_ecn_2d", out.str()); out.str("");

    out << "s" << params->n_dctcp << ":" << "s" << params->n_reno << " " << res->rr_static << std::endl;
    writeToFile("rr_2d", out.str()); out.str("");

    out << "s" << params->n_dctcp << ":" << "s" << params->n_reno << " " << res->wr_static << std::endl;
    writeToFile("wr_2d", out.str()); out.str("");

    out << "s" << params->n_dctcp  << ":" << "s" << params->n_reno <<  " " << res->util->average() << " " << res->util->p(99) << " " << res->util->p(1) << std::endl;
    writeToFile("util_2d", out.str()); out.str("");

    return 0;
}