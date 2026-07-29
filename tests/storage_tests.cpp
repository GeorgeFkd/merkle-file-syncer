#include "LocalFileStorage.h"
#include "S3FileStorage.h"
#include <QCoreApplication>
#include <QDir>
#include <QUuid>
#include <gtest/gtest.h>

struct LocalStorageTag {
  static std::unique_ptr<FileStorage> makeStorage(const QString &rootPath) {
    auto s = std::make_unique<LocalFileStorage>();
    s->setRoot(rootPath);
    return s;
  }
};

struct S3StorageTag {
  static std::unique_ptr<FileStorage> makeStorage(const QString &) {
    auto s = std::make_unique<S3FileStorage>();
    s->init(S3Config{.endpoint = "localhost:9000",
                     .accessKey = "minioadmin",
                     .secretKey = "minioadmin",
                     .bucket = "test-bucket",
                     .useSSL = false});
    return s;
  }
};

using StorageImplementations = ::testing::Types<LocalStorageTag, S3StorageTag>;

template <typename Tag> class StorageTest : public ::testing::Test {
protected:
  void SetUp() override {
    runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    rootDir = QDir(QDir::tempPath() + "/test_storage/" + runId);
    QDir().mkpath(rootDir.path());

    storage = Tag::makeStorage(rootDir.path());
    storage->cleanup(user);
    storage->cleanup(otherUser);
  }

  void TearDown() override {
    storage->cleanup(user);
    storage->cleanup(otherUser);
    QDir(rootDir.path()).removeRecursively();
  }

  QString runId;
  QDir rootDir;
  QString user = "alice";
  QString otherUser = "bob";
  std::unique_ptr<FileStorage> storage;
};

TYPED_TEST_SUITE(StorageTest, StorageImplementations);

// --- Round-trip ---

TYPED_TEST(StorageTest, writeThenReadReturnsSameBytes) {
  QByteArray contents = "hello world";
  ASSERT_TRUE(this->storage->writeFile(this->user, "test.txt", contents));
  auto read = this->storage->readFile(this->user, "test.txt");
  ASSERT_TRUE(read.has_value());
  ASSERT_EQ(read.value(), contents);
}

TYPED_TEST(StorageTest, writeThenReadNestedPath) {
  QByteArray contents = "nested content";
  ASSERT_TRUE(this->storage->writeFile(this->user, "a/b/c/test.txt", contents));
  auto read = this->storage->readFile(this->user, "a/b/c/test.txt");
  ASSERT_TRUE(read.has_value());
  ASSERT_EQ(read.value(), contents);
}

TYPED_TEST(StorageTest, deleteRemovesFile) {
  ASSERT_TRUE(this->storage->writeFile(this->user, "test.txt", "bye"));
  ASSERT_TRUE(this->storage->deleteFile(this->user, "test.txt"));
  auto read = this->storage->readFile(this->user, "test.txt");
  ASSERT_FALSE(read.has_value());
}

TYPED_TEST(StorageTest, deleteRemovesFromListing) {
  ASSERT_TRUE(this->storage->writeFile(this->user, "test.txt", "bye"));
  ASSERT_TRUE(this->storage->deleteFile(this->user, "test.txt"));
  auto files = this->storage->listFiles(this->user);
  ASSERT_FALSE(files.contains("test.txt"));
}

// --- Listing semantics ---

TYPED_TEST(StorageTest, listFilesReturnsRelativePaths) {
  this->storage->writeFile(this->user, "a/b/c.txt", "x");
  auto files = this->storage->listFiles(this->user);
  ASSERT_EQ(files.size(), 1);
  ASSERT_EQ(files[0], "a/b/c.txt");
}

TYPED_TEST(StorageTest, listFilesEmptyUserReturnsEmpty) {
  auto files = this->storage->listFiles(this->user);
  ASSERT_TRUE(files.isEmpty());
}

TYPED_TEST(StorageTest, listFilesIncludesNestedFiles) {
  this->storage->writeFile(this->user, "top.txt", "1");
  this->storage->writeFile(this->user, "dir/middle.txt", "2");
  this->storage->writeFile(this->user, "dir/deep/leaf.txt", "3");
  auto files = this->storage->listFiles(this->user);
  ASSERT_EQ(files.size(), 3);
  ASSERT_TRUE(files.contains("top.txt"));
  ASSERT_TRUE(files.contains("dir/middle.txt"));
  ASSERT_TRUE(files.contains("dir/deep/leaf.txt"));
}

TYPED_TEST(StorageTest, crossUserIsolation) {
  this->storage->writeFile(this->user, "shared.txt", "alice's");
  this->storage->writeFile(this->otherUser, "shared.txt", "bob's");

  auto aliceFiles = this->storage->listFiles(this->user);
  auto bobFiles = this->storage->listFiles(this->otherUser);

  ASSERT_EQ(aliceFiles.size(), 1);
  ASSERT_EQ(bobFiles.size(), 1);

  auto aliceContents = this->storage->readFile(this->user, "shared.txt");
  auto bobContents = this->storage->readFile(this->otherUser, "shared.txt");
  ASSERT_EQ(aliceContents.value(), QByteArray("alice's"));
  ASSERT_EQ(bobContents.value(), QByteArray("bob's"));
}

// --- Edge cases ---
// this is important to be tested bcs on S3 when you say length=0 it returns the
// whole file
TYPED_TEST(StorageTest, readRangeZeroLengthReturnsEmpty) {
  this->storage->writeFile(this->user, "f.txt", "hello");
  auto chunk = this->storage->readRange(this->user, "f.txt", 0, 0);
  ASSERT_TRUE(chunk.has_value());
  ASSERT_EQ(chunk.value().size(), 0);
}

TYPED_TEST(StorageTest, emptyFileRoundTrips) {
  ASSERT_TRUE(this->storage->writeFile(this->user, "empty.txt", QByteArray{}));
  auto read = this->storage->readFile(this->user, "empty.txt");
  ASSERT_TRUE(read.has_value());
  ASSERT_EQ(read.value().size(), 0);
}

TYPED_TEST(StorageTest, binaryContentRoundTrips) {
  QByteArray contents;
  for (int i = 0; i < 256; ++i) {
    contents.append(static_cast<char>(i));
  }
  ASSERT_TRUE(this->storage->writeFile(this->user, "binary.bin", contents));
  auto read = this->storage->readFile(this->user, "binary.bin");
  ASSERT_TRUE(read.has_value());
  ASSERT_EQ(read.value(), contents);
}

TYPED_TEST(StorageTest, overwriteKeepsSecondVersion) {
  this->storage->writeFile(this->user, "file.txt", "first");
  this->storage->writeFile(this->user, "file.txt", "second");
  auto read = this->storage->readFile(this->user, "file.txt");
  ASSERT_TRUE(read.has_value());
  ASSERT_EQ(read.value(), QByteArray("second"));
}

TYPED_TEST(StorageTest, readNonexistentReturnsNullopt) {
  auto read = this->storage->readFile(this->user, "missing.txt");
  ASSERT_FALSE(read.has_value());
}

// --- Cleanup ---

TYPED_TEST(StorageTest, cleanupEmptiesUserNamespace) {
  this->storage->writeFile(this->user, "a.txt", "1");
  this->storage->writeFile(this->user, "b/c.txt", "2");
  this->storage->cleanup(this->user);
  auto files = this->storage->listFiles(this->user);
  ASSERT_TRUE(files.isEmpty());
}

TYPED_TEST(StorageTest, cleanupDoesNotAffectOtherUsers) {
  this->storage->writeFile(this->user, "a.txt", "alice");
  this->storage->writeFile(this->otherUser, "a.txt", "bob");
  this->storage->cleanup(this->user);

  auto aliceFiles = this->storage->listFiles(this->user);
  auto bobFiles = this->storage->listFiles(this->otherUser);
  ASSERT_TRUE(aliceFiles.isEmpty());
  ASSERT_EQ(bobFiles.size(), 1);
}

// --- fileSize ---

TYPED_TEST(StorageTest, fileSizeMatchesWrittenLength) {
  QByteArray contents(1234, 'x');
  ASSERT_TRUE(this->storage->writeFile(this->user, "sized.bin", contents));
  auto size = this->storage->fileSize(this->user, "sized.bin");
  ASSERT_TRUE(size.has_value());
  ASSERT_EQ(size.value(), 1234);
}

TYPED_TEST(StorageTest, fileSizeOfEmptyFileIsZero) {
  ASSERT_TRUE(this->storage->writeFile(this->user, "empty.bin", QByteArray{}));
  auto size = this->storage->fileSize(this->user, "empty.bin");
  ASSERT_TRUE(size.has_value());
  ASSERT_EQ(size.value(), 0);
}

TYPED_TEST(StorageTest, fileSizeOfNonexistentReturnsNullopt) {
  auto size = this->storage->fileSize(this->user, "missing.bin");
  ASSERT_FALSE(size.has_value());
}

TYPED_TEST(StorageTest, fileSizeUpdatesAfterOverwrite) {
  this->storage->writeFile(this->user, "f.bin", QByteArray(100, 'a'));
  this->storage->writeFile(this->user, "f.bin", QByteArray(500, 'b'));
  auto size = this->storage->fileSize(this->user, "f.bin");
  ASSERT_TRUE(size.has_value());
  ASSERT_EQ(size.value(), 500);
}

// --- readRange ---

TYPED_TEST(StorageTest, readRangeFullFileEqualsReadFile) {
  QByteArray contents = "the quick brown fox jumps over the lazy dog";
  this->storage->writeFile(this->user, "f.txt", contents);

  auto whole = this->storage->readFile(this->user, "f.txt");
  auto ranged =
      this->storage->readRange(this->user, "f.txt", 0, contents.size());

  ASSERT_TRUE(whole.has_value());
  ASSERT_TRUE(ranged.has_value());
  ASSERT_EQ(whole.value(), ranged.value());
}

TYPED_TEST(StorageTest, readRangePrefix) {
  QByteArray contents = "0123456789abcdef";
  this->storage->writeFile(this->user, "f.txt", contents);

  auto first10 = this->storage->readRange(this->user, "f.txt", 0, 10);
  ASSERT_TRUE(first10.has_value());
  ASSERT_EQ(first10.value(), QByteArray("0123456789"));
}

TYPED_TEST(StorageTest, readRangeSuffix) {
  QByteArray contents = "0123456789abcdef";
  this->storage->writeFile(this->user, "f.txt", contents);

  auto last6 = this->storage->readRange(this->user, "f.txt", 10, 6);
  ASSERT_TRUE(last6.has_value());
  ASSERT_EQ(last6.value(), QByteArray("abcdef"));
}

TYPED_TEST(StorageTest, readRangeMiddle) {
  QByteArray contents = "0123456789abcdef";
  this->storage->writeFile(this->user, "f.txt", contents);

  auto middle = this->storage->readRange(this->user, "f.txt", 4, 6);
  ASSERT_TRUE(middle.has_value());
  ASSERT_EQ(middle.value(), QByteArray("456789"));
}

TYPED_TEST(StorageTest, readRangeAcrossChunkBoundary) {
  // 2MB file, read 100 bytes straddling the 1MB mark
  // 1MB specifically bcs we will probably use it as
  // the size to cut files into
  QByteArray contents;
  contents.reserve(2 * 1024 * 1024);
  for (int i = 0; i < 2 * 1024 * 1024; ++i) {
    contents.append(static_cast<char>(i & 0xFF));
  }
  this->storage->writeFile(this->user, "big.bin", contents);

  qint64 offset = 1024 * 1024 - 50;
  qint64 length = 100;
  auto chunk = this->storage->readRange(this->user, "big.bin", offset, length);
  ASSERT_TRUE(chunk.has_value());
  ASSERT_EQ(chunk.value(), contents.mid(offset, length));
}

TYPED_TEST(StorageTest, readRangeOnNonexistentReturnsNullopt) {
  auto chunk = this->storage->readRange(this->user, "missing.bin", 0, 10);
  ASSERT_FALSE(chunk.has_value());
}

TYPED_TEST(StorageTest, readRangeBinaryContentRoundTrips) {
  QByteArray contents;
  for (int i = 0; i < 256; ++i) {
    contents.append(static_cast<char>(i));
  }
  this->storage->writeFile(this->user, "binary.bin", contents);

  auto chunk = this->storage->readRange(this->user, "binary.bin", 0, 256);
  ASSERT_TRUE(chunk.has_value());
  ASSERT_EQ(chunk.value(), contents);
}

static constexpr qint64 S3_MIN_PART_SIZE = 5 * 1024 * 1024;

TYPED_TEST(StorageTest, chunkedWriteRoundTripsHappyPathUsage) {
  const qint64 chunkSize = S3_MIN_PART_SIZE;
  QByteArray part1(chunkSize, 'a');
  QByteArray part2(1234, 'b');
  QByteArray original = part1 + part2;

  ASSERT_TRUE(this->storage->beginWrite(this->user, "ooo.bin",
                                        original.size(), chunkSize));
  // part 2 first, then part 1
  ASSERT_TRUE(this->storage->writeRange(this->user, "ooo.bin", 2, chunkSize,
                                        part2));
  ASSERT_TRUE(this->storage->writeRange(this->user, "ooo.bin", 1, 0, part1));
  ASSERT_TRUE(this->storage->finishWrite(this->user, "ooo.bin"));

  auto read = this->storage->readFile(this->user, "ooo.bin");
  ASSERT_TRUE(read.has_value());
  ASSERT_EQ(read.value(), original);
}

TYPED_TEST(StorageTest, chunkedWriteOutOfOrder) {
  const qint64 chunkSize = S3_MIN_PART_SIZE;
  QByteArray part1(chunkSize, 'a');
  QByteArray part2(1234, 'b');
  QByteArray original = part1 + part2;

  ASSERT_TRUE(this->storage->beginWrite(this->user, "ooo.bin",
                                        original.size(), chunkSize));
  // part 2 first, then part 1
  ASSERT_TRUE(this->storage->writeRange(this->user, "ooo.bin", 2, chunkSize,
                                        part2));
  ASSERT_TRUE(this->storage->writeRange(this->user, "ooo.bin", 1, 0, part1));
  ASSERT_TRUE(this->storage->finishWrite(this->user, "ooo.bin"));

  auto read = this->storage->readFile(this->user, "ooo.bin");
  ASSERT_TRUE(read.has_value());
  ASSERT_EQ(read.value(), original);
}


TYPED_TEST(StorageTest, fileInvisibleUntilFinish) {
  const qint64 chunkSize = S3_MIN_PART_SIZE;
  QByteArray part1(chunkSize, 'a');
  QByteArray part2(1234, 'b');
  const qint64 total = part1.size() + part2.size();

  ASSERT_TRUE(this->storage->beginWrite(this->user, "pending.bin", total,
                                        chunkSize));
  ASSERT_TRUE(this->storage->writeRange(this->user, "pending.bin", 1, 0,
                                        part1));

  auto midRead = this->storage->readFile(this->user, "pending.bin");
  ASSERT_FALSE(midRead.has_value());

  ASSERT_TRUE(this->storage->writeRange(this->user, "pending.bin", 2, chunkSize,
                                        part2));
  ASSERT_TRUE(this->storage->finishWrite(this->user, "pending.bin"));

  auto finalRead = this->storage->readFile(this->user, "pending.bin");
  ASSERT_TRUE(finalRead.has_value());
}

// abortWrite discards everything; nothing lands at the real path.
TYPED_TEST(StorageTest, abortDiscardsPartialWrite) {
  ASSERT_TRUE(this->storage->beginWrite(this->user, "aborted.bin", 20, 10));
  ASSERT_TRUE(this->storage->writeRange(this->user, "aborted.bin", 1, 0,
                                        QByteArray(10, 'x')));
  ASSERT_TRUE(this->storage->abortWrite(this->user, "aborted.bin"));

  auto read = this->storage->readFile(this->user, "aborted.bin");
  ASSERT_FALSE(read.has_value());
}

// writeRange with no active transfer fails rather than crashing.
TYPED_TEST(StorageTest, writeRangeWithoutBeginFails) {
  EXPECT_DEATH(this->storage->writeRange(this->user, "nope.bin", 1, 0,
                                         QByteArray("data")),
               "");
}

// finishWrite with no active transfer fails.
TYPED_TEST(StorageTest, finishWithoutBeginFails) {
  EXPECT_DEATH(this->storage->finishWrite(this->user, "nope.bin"), "");
}

TYPED_TEST(StorageTest, abortWithoutBeginDies) {
  EXPECT_DEATH(this->storage->abortWrite(this->user, "nope.bin"), "");
}

#include <rapidcheck/gtest.h>

// Property checked: no matter how i slice a file into ranges, reading by
// chunking should equal reading the whole file
RC_GTEST_TYPED_FIXTURE_PROP(StorageTest, anyPartitionReassemblesToWholeFile,
                            (const std::vector<uint8_t> &bytes)) {
  RC_PRE(!bytes.empty());

  QByteArray contents(reinterpret_cast<const char *>(bytes.data()),
                      static_cast<int>(bytes.size()));
  this->storage->writeFile(this->user, "prop.bin", contents);

  // Generate a random partition: a list of cut points, sorted.
  auto cutPoints = *rc::gen::container<std::vector<int>>(
      rc::gen::inRange(0, static_cast<int>(bytes.size())));
  std::sort(cutPoints.begin(), cutPoints.end());
  cutPoints.erase(std::unique(cutPoints.begin(), cutPoints.end()),
                  cutPoints.end());
  cutPoints.insert(cutPoints.begin(), 0);
  cutPoints.push_back(static_cast<int>(bytes.size()));

  QByteArray reassembled;
  for (size_t i = 0; i + 1 < cutPoints.size(); ++i) {
    qint64 offset = cutPoints[i];
    qint64 length = cutPoints[i + 1] - cutPoints[i];
    auto chunk =
        this->storage->readRange(this->user, "prop.bin", offset, length);
    RC_ASSERT(chunk.has_value());
    reassembled.append(chunk.value());
  }

  RC_ASSERT(reassembled == contents);

  this->storage->deleteFile(this->user, "prop.bin");
}
