#include "../src/DatabaseManager.h"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>

void testAddCorrectionAndGetVocabulary() {
  std::string tmpDir = "/tmp/punch2pen_test_db";
  std::filesystem::create_directories(tmpDir);
  std::string dbPath = tmpDir + "/test_corrections.csv";

  punch2pen::DatabaseManager db;
  db.initialize(dbPath);

  db.addCorrection("helo", "hello");
  db.addCorrection("wrld", "world");

  auto vocab = db.getVocabulary();
  assert(!vocab.empty() && "Vocabulary should not be empty after corrections");

  bool foundHello = std::find(vocab.begin(), vocab.end(), "hello") != vocab.end();
  bool foundWorld = std::find(vocab.begin(), vocab.end(), "world") != vocab.end();
  assert(foundHello && "Vocabulary should contain 'hello'");
  assert(foundWorld && "Vocabulary should contain 'world'");

  std::cout << "[PASS] testAddCorrectionAndGetVocabulary" << std::endl;

  std::filesystem::remove_all(tmpDir);
}

void testPersistence() {
  std::string tmpDir = "/tmp/punch2pen_test_db_persist";
  std::filesystem::create_directories(tmpDir);
  std::string dbPath = tmpDir + "/test_corrections.csv";

  {
    punch2pen::DatabaseManager db;
    db.initialize(dbPath);
    db.addCorrection("foo", "bar baz");
  }

  {
    punch2pen::DatabaseManager db2;
    db2.initialize(dbPath);
    auto vocab = db2.getVocabulary();

    bool foundBar = std::find(vocab.begin(), vocab.end(), "bar") != vocab.end();
    bool foundBaz = std::find(vocab.begin(), vocab.end(), "baz") != vocab.end();
    assert(foundBar && "Persisted vocabulary should contain 'bar'");
    assert(foundBaz && "Persisted vocabulary should contain 'baz'");
  }

  std::cout << "[PASS] testPersistence" << std::endl;

  std::filesystem::remove_all(tmpDir);
}

void testMultiWordCorrection() {
  std::string tmpDir = "/tmp/punch2pen_test_db_multi";
  std::filesystem::create_directories(tmpDir);
  std::string dbPath = tmpDir + "/test_corrections.csv";

  punch2pen::DatabaseManager db;
  db.initialize(dbPath);

  db.addCorrection("jn", "John Smith");
  auto vocab = db.getVocabulary();

  bool foundJohn = std::find(vocab.begin(), vocab.end(), "John") != vocab.end();
  bool foundSmith = std::find(vocab.begin(), vocab.end(), "Smith") != vocab.end();
  assert(foundJohn && "Vocabulary should contain 'John'");
  assert(foundSmith && "Vocabulary should contain 'Smith'");

  std::cout << "[PASS] testMultiWordCorrection" << std::endl;

  std::filesystem::remove_all(tmpDir);
}

void testEmptyDatabase() {
  std::string tmpDir = "/tmp/punch2pen_test_db_empty";
  std::filesystem::create_directories(tmpDir);
  std::string dbPath = tmpDir + "/test_corrections.csv";

  punch2pen::DatabaseManager db;
  db.initialize(dbPath);

  auto vocab = db.getVocabulary();
  assert(vocab.empty() && "Fresh database should have empty vocabulary");

  std::cout << "[PASS] testEmptyDatabase" << std::endl;

  std::filesystem::remove_all(tmpDir);
}

int main() {
  testAddCorrectionAndGetVocabulary();
  testPersistence();
  testMultiWordCorrection();
  testEmptyDatabase();
  std::cout << "All DatabaseManager tests passed!" << std::endl;
  return 0;
}
