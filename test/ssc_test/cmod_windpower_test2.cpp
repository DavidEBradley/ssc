#include <gtest/gtest.h>
#include <string>

#include "core.h"
#include "vartab.h"
#include "common.h"

#include "cmod_windpower_test2.h"

namespace {
	using ::testing::TestWithParam;
	//typedef SimulationTestTable* CreateSimulationTestTableFunc();

}

class computeModuleTest : public TestWithParam<SimulationTestTable*> {
public:
	void SetUp() {
		table_ = GetParam();
		data_ = ssc_data_create();
		ssc_module_exec_set_print(0);
		EXPECT_FALSE(testTableToSSCData());
	}
	void TearDown() {
		ssc_data_free(data_);
	}
	
	/// runs compute module and returns true if successful
	bool compute() {
		ssc_module_t module = ssc_module_create(table_->getCMODType());
		if (NULL == module)
		{
			printf("error: could not create %s module.", table_->getCMODType());
			return false;
		}
		if (ssc_module_exec(module, data_) == 0)
		{
			printf("error during simulation.");
			ssc_module_free(module);
			return false;
		}
		ssc_module_free(module);
		return true;
	}

protected:
	SimulationTestTable * table_;
	ssc_data_t data_;

private:
	/// Takes the input values in TestInfo to assign variables in ssc_data_t
	bool testTableToSSCData() {
		for (int i = 0; i < table_->getNumInfo(); i++) {
			const TestInfo* info = &(table_->getInfo())[i];
			if (info->dataType == STR) {
				ssc_data_set_string(data_, info->sscVarName, info->values);
			}
			else if (info->dataType == NUM) {
				ssc_data_set_number(data_, info->sscVarName, (ssc_number_t)atof(info->values));
			}
			else if (info->dataType == ARR) {
				size_t n = info->length;
				std::stringstream ss(info->values);
				ssc_number_t* val = new ssc_number_t[n]();
				for (size_t j = 0; j < n; j++) {
					std::string substr;
					getline(ss, substr, ',');
					val[i] = (ssc_number_t)atof(substr.c_str());
				}
				ssc_data_set_array(data_, info->sscVarName, val, (int)n);
			}
			else if (info->dataType == MAT) {
				size_t n = info->length;
				size_t m = info->width;
				std::stringstream ss(info->values);
				ssc_number_t* val = new ssc_number_t[n*m]();
				for (size_t j = 0; j < n*m; j++) {
					std::string substr;
					getline(ss, substr, ',');
					val[i] = (ssc_number_t)atof(substr.c_str());
				}
				ssc_data_set_matrix(data_, info->sscVarName, val, (int)n, (int)m);
			}
			else {
				return false;
			}
		}
		return true;
	}
};

/// Run all tests in TestResult
TEST_P(computeModuleTest, RunSimulationTest) {
	EXPECT_TRUE(true);
	int n = table_->getNumResult();
	for (int i = 0; i < n; i++) {
		bool boolCompute = compute();

		const TestResult* testResult = &(table_->getResult())[i];
		if (testResult->testType == ERR) {
			EXPECT_FALSE(boolCompute);
		}

		ssc_number_t actualResult = 0.0;
		std::stringstream ss(testResult->sscVarName);
		std::string varName, index;
		size_t varIndex;
		getline(ss, varName, '[');
		if (varName.length() < strlen(testResult->sscVarName)) {
			getline(ss, index, ']');
			varIndex = (int)atof(index.c_str());
			actualResult = ssc_data_get_array(data_, testResult->sscVarName, nullptr)[varIndex];
		}
		else {
			ssc_data_get_number(data_, testResult->sscVarName, &actualResult);
		}

		std::string failureMsg = testResult->sscVarName  + ':' + std::to_string(actualResult) + " / " + std::to_string(testResult->expectedResult);
		if (testResult->testType == EQ || testResult->testType == TF) {
			EXPECT_EQ(actualResult, testResult->expectedResult) << failureMsg;
		}
		else if (testResult->testType == NR) {
			EXPECT_NEAR(actualResult, testResult->expectedResult, testResult->errorBound) << failureMsg;
		}
		else if (testResult->testType == GT) {
			EXPECT_GT(actualResult, testResult->expectedResult) << failureMsg;
		}
		else if (testResult->testType == LT) {
			EXPECT_LT(actualResult, testResult->expectedResult) << failureMsg;
		}
		else {
			EXPECT_TRUE(1);
		}
	}
}


INSTANTIATE_TEST_CASE_P(Testin, computeModuleTest, testing::Values(&windpowerTestDefault, &windpowerTestDefault));
