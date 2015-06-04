/*
 *  qsignal.cpp
 *
 *  Created by Tobias Wood on 2015/06/02.
 *  Copyright (c) 2015 Tobias Wood.
 *
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include <string>
#include <iostream>
#include <getopt.h>
#include <exception>
#include <Eigen/Dense>

#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"
#include "itkVectorImage.h"
#include "itkImageToImageFilter.h"
#include "itkComplexToModulusImageFilter.h"

#include "Filters/VectorToImageFilter.h"

#include "Model.h"
#include "Sequence.h"
#include "Util.h"

using namespace std;
using namespace Eigen;
using namespace QI;

//******************************************************************************
// Filter
//******************************************************************************
typedef itk::Image<float, 3> TImage;
typedef itk::VectorImage<float, 3> TVImage;
typedef itk::VectorImage<complex<float>, 3> TCVImage;

class SignalsFilter : public itk::ImageToImageFilter<TImage, TCVImage> {
private:
	shared_ptr<SequenceBase> m_sequence;
	shared_ptr<Model> m_model;

public:
	/** Standard class typedefs. */
	typedef SignalsFilter                        Self;
	typedef ImageToImageFilter<TImage, TCVImage> Superclass;
	typedef itk::SmartPointer<Self>                   Pointer;
	typedef typename TImage::RegionType          RegionType;

	itkNewMacro(Self); /** Method for creation through the object factory. */
	itkTypeMacro(Self, Superclass); /** Run-time type information (and related methods). */

	void SetInput(const size_t i, const TImage *img) {
		//std::cout <<  __PRETTY_FUNCTION__ << endl;
		if (i < m_model->nParameters()) {
			this->SetNthInput(i, const_cast<TImage*>(img));
		} else {
			throw(runtime_error("Const input out of range"));
		}
	}
	void SetMask(const TImage *mask) {
		//std::cout <<  __PRETTY_FUNCTION__ << endl;
		this->SetNthInput(m_model->nParameters(), const_cast<TImage*>(mask));
	}

	typename TImage::ConstPointer GetInput(const size_t i) const {
		//std::cout <<  __PRETTY_FUNCTION__ << endl;
		if (i < m_model->nParameters()) {
			return static_cast<const TImage *> (this->ProcessObject::GetInput(i));
		} else {
			throw(runtime_error("Get Data Input out of range."));
		}
	}

	typename TImage::ConstPointer GetMask() const {
		//std::cout <<  __PRETTY_FUNCTION__ << endl;
		return static_cast<const TImage *>(this->ProcessObject::GetInput(m_model->nParameters()));
	}

	TCVImage *GetOutput() {
		//std::cout <<  __PRETTY_FUNCTION__ << endl;
		return dynamic_cast<TCVImage *>(this->ProcessObject::GetOutput(0));
	}

	void SetSequence(shared_ptr<SequenceBase> s) {
		m_sequence = s;
		this->SetNumberOfRequiredOutputs(1);
		this->SetNthOutput(0, this->MakeOutput(0));
	}
	void SetModel(shared_ptr<Model> m) {
		m_model = m;
		this->SetNumberOfRequiredInputs(m_model->nParameters());
	}

	virtual void GenerateOutputInformation() override {
		//std::cout <<  __PRETTY_FUNCTION__ << endl;
		Superclass::GenerateOutputInformation();
		const auto op = this->GetOutput();
		op->SetRegions(this->GetInput(0)->GetLargestPossibleRegion());
		op->SetNumberOfComponentsPerPixel(m_sequence->size());
		op->Allocate();
	}

	virtual void Update() override {
		//std::cout << __PRETTY_FUNCTION__ << endl;
		Superclass::Update();
	}

protected:
	SignalsFilter() {}
	~SignalsFilter(){}

	virtual void ThreadedGenerateData(const RegionType &region, itk::ThreadIdType threadId) {
		//std::cout <<  __PRETTY_FUNCTION__ << endl;
		vector<itk::ImageRegionConstIterator<TImage>> inIters(m_model->nParameters());
		for (size_t i = 0; i < m_model->nParameters(); i++) {
			inIters[i] = itk::ImageRegionConstIterator<TImage>(this->GetInput(i), region);
		}
		itk::ImageRegionConstIterator<TImage> maskIter;
		if (this->GetMask()) {
			maskIter = itk::ImageRegionConstIterator<TImage>(this->GetMask(), region);
		}
		itk::ImageRegionIterator<TCVImage> outputIter(this->GetOutput(), region);
		while(!inIters[0].IsAtEnd()) {
			typename TImage::ConstPointer m = this->GetMask();
			if (!m || maskIter.Get()) {
				VectorXd parameters(m_model->nParameters());
				for (size_t i = 0; i < inIters.size(); i++) {
					parameters[i] = inIters[i].Get();
				}
				VectorXcf allData = m_sequence->signal(m_model, parameters).cast<complex<float>>();
				itk::VariableLengthVector<complex<float>> dataVector(allData.data(), m_sequence->size());
				outputIter.Set(dataVector);
			}
			if (this->GetMask())
				++maskIter;
			for (size_t i = 0; i < m_model->nParameters(); i++) {
					++inIters[i];
			}
			++outputIter;
		}
	}

	itk::DataObject::Pointer MakeOutput(unsigned int idx) {
		//std::cout <<  __PRETTY_FUNCTION__ << endl;
		itk::DataObject::Pointer output;
		if (idx < 1) {
			auto img = TCVImage::New();
			img->SetNumberOfComponentsPerPixel(m_sequence->size());
			output = img;
		} else {
			std::cerr << "No output " << idx << std::endl;
			output = NULL;
		}
		return output.GetPointer();
	}

private:
	SignalsFilter(const Self &); //purposely not implemented
	void operator=(const Self &);  //purposely not implemented
};


//******************************************************************************
// Arguments / Usage
//******************************************************************************
const string usage {
"Usage is: mcsignal [options]\n\
\n\
Calculates multi-component DESPOT signals (mainly for testing purposes).\n\
The program will prompt for input (unless --no-prompt specified)\n\
\n\
All times (TR) are in SECONDS. All angles are in degrees.\n\
\n\
Options:\n\
	--help, -h        : Print this message.\n\
	--verbose, -v     : Print extra information.\n\
	--mask, -m file   : Only calculate inside the mask.\n\
	--out, -o path    : Add a prefix to the output filenames\n\
	--no-prompt, -n   : Don't print prompts for input.\n\
	--noise, -N val   : Add complex noise with std=val.\n\
	--1, --2, --3     : Use 1, 2 or 3 component sequences (default 3).\n\
	--complex, -x     : Output complex-valued signal.\n\
	--sequences, -M s : Use simple sequences (default).\n\
	            f     : Use Finite Pulse Length correction.\n\
	--threads, -T N   : Use N threads (default=hardware limit)\n"
};

static shared_ptr<Model> model = make_shared<SCD>();
static bool verbose = false, prompt = true, finitesequences = false, outputComplex = false;
static string outPrefix = "";
static double sigma = 0.;
static struct option long_opts[] = {
	{"help", no_argument, 0, 'h'},
	{"verbose", no_argument, 0, 'v'},
	{"mask", required_argument, 0, 'm'},
	{"out", required_argument, 0, 'o'},
	{"no-prompt", no_argument, 0, 'n'},
	{"noise", required_argument, 0, 'N'},
	{"1", no_argument, 0, '1'},
	{"2", no_argument, 0, '2'},
	{"3", no_argument, 0, '3'},
	{"complex", no_argument, 0, 'x'},
	{"sequences", no_argument, 0, 'M'},
	{"threads", required_argument, 0, 'T'},
	{0, 0, 0, 0}
};
static const char *short_opts = "hvnN:m:o:123xM:T:";
//******************************************************************************
#pragma mark Read in all required files and data from cin
//******************************************************************************
void parseInput(vector<shared_ptr<SequenceBase>> &cs, vector<string> &names);
void parseInput(vector<shared_ptr<SequenceBase>> &cs, vector<string> &names) {
	string type;
	if (prompt) cout << "Specify next signal type (SPGR/SSFP): " << flush;
	while (Read(cin, type) && (type != "END") && (type != "")) {
		if (type == "SPGR") {
			cs.push_back(make_shared<SPGRSimple>(prompt));
		} else if (type == "SPGRFinite") {
			cs.push_back(make_shared<SPGRFinite>(prompt));
		} else if (type == "SSFP") {
			cs.push_back(make_shared<SSFPSimple>(prompt));
		} else if (type == "SSFPFinite") {
			cs.push_back(make_shared<SSFPFinite>(prompt));
		} else if (type == "SSFPEllipse") {
			cs.push_back(make_shared<SSFPEllipse>(prompt));
		} else if (type == "IRSPGR") {
			cs.push_back(make_shared<IRSPGR>(prompt));
		} else if (type == "MPRAGE") {
			cs.push_back(make_shared<MPRAGE>(prompt));
		} else if (type == "AFI") {
			cs.push_back(make_shared<AFI>(prompt));
		} else if (type == "SPINECHO") {
			cs.push_back(make_shared<MultiEcho>(prompt));
		} else {
			throw(std::runtime_error("Unknown signal type: " + type));
		}
		string filename;
		if (prompt) cout << "Enter output filename: " << flush;
		Read(cin, filename);
		names.push_back(filename);
		// Print message ready for next loop
		if (prompt) cout << "Specify next image type (SPGR/SSFP, END to finish input): " << flush;
	}
}
//******************************************************************************
// Main
//******************************************************************************
int main(int argc, char **argv)
{
	Eigen::initParallel();
	QI::ReadImageF::Pointer mask = ITK_NULLPTR;
	int indexptr = 0, c;
	while ((c = getopt_long(argc, argv, short_opts, long_opts, &indexptr)) != -1) {
		switch (c) {
			case 'v': verbose = true; break;
			case 'n': prompt = false; break;
			case 'N': sigma = atof(optarg); break;
			case 'm':
				cout << "Reading mask file " << optarg << endl;
				mask = QI::ReadImageF::New();
				mask->SetFileName(optarg);
				break;
			case 'o':
				outPrefix = optarg;
				cout << "Output prefix will be: " << outPrefix << endl;
				break;
			case '1': model = make_shared<SCD>(); break;
			case '2': model = make_shared<MCD2>(); break;
			case '3': model = make_shared<MCD3>(); break;
			case 'x': outputComplex = true; break;
			case 'M':
				switch (*optarg) {
					case 's': finitesequences = false; if (prompt) cout << "Simple sequences selected." << endl; break;
					case 'f': finitesequences = true; if (prompt) cout << "Finite pulse correction selected." << endl; break;
					default:
						cout << "Unknown sequences type " << *optarg << endl;
						return EXIT_FAILURE;
						break;
				}
				break;
			case 'T': itk::MultiThreader::SetGlobalDefaultNumberOfThreads(atoi(optarg)); break;
			case 'h':
			case '?': // getopt will print an error message
			default:
				cout << usage << endl;
				return EXIT_FAILURE;
		}
	}
	if ((argc - optind) != 0) {
		cerr << usage << endl << "Incorrect number of arguments." << endl;
		return EXIT_FAILURE;
	}
	//if (verbose) cout << version << endl << credit_me << endl;
	if (verbose) cout << "Using " << model->Name() << " model." << endl;
	/***************************************************************************
	 * Read in parameter files
	 **************************************************************************/
	SignalsFilter::Pointer calcSignal = SignalsFilter::New();
	calcSignal->SetModel(model);
	vector<QI::ReadImageF::Pointer> pFiles(model->nParameters());
	if (prompt) cout << "Loading parameters." << endl;
	for (size_t i = 0; i < model->nParameters(); i++) {
		if (prompt) cout << "Enter path to " << model->Names()[i] << " file: " << flush;
		string filename;
		getline(cin, filename);
		if (verbose) cout << "Opening " << filename << endl;
		pFiles[i] = QI::ReadImageF::New();
		pFiles[i]->SetFileName(filename);
		calcSignal->SetInput(i, pFiles[i]->GetOutput());
	}

	/***************************************************************************
	 * Set up sequences
	 **************************************************************************/
	vector<shared_ptr<SequenceBase>> sequences;
	vector<string> filenames;
	parseInput(sequences, filenames);
	for (size_t i = 0; i < sequences.size(); i++) {
		if (verbose) {
			cout << "Calculating sequence: " << endl << *(sequences[i]);
		}
		calcSignal->SetSequence(sequences[i]);
		auto VecTo4D = QI::VectorToTimeseriesXF::New();
		VecTo4D->SetInput(calcSignal->GetOutput());
		if (outputComplex) {
			auto writer = QI::WriteTimeseriesXF::New();
			writer->SetInput(VecTo4D->GetOutput());
			writer->SetFileName(filenames[i]);
			writer->Update();
		} else {
			auto writer = QI::WriteTimeseriesF::New();
			auto abs = itk::ComplexToModulusImageFilter<QI::TimeseriesXF, QI::TimeseriesF>::New();
			abs->SetInput(VecTo4D->GetOutput());
			writer->SetInput(abs->GetOutput());
			writer->SetFileName(filenames[i]);
			writer->Update();
		}
	}
	if (verbose) cout << "Finished all sequences." << endl;
	return EXIT_SUCCESS;
}

