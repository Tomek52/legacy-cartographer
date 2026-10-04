// Verifies: b12-query-and-export-v2 (criterion 1: outputs inside a git work
// tree that its ignore files do not exclude are detected, without running
// git; rules of ADR-0016).
#include "cli/git_exposure.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;
using lcart::cli::unignored_git_work_tree;

class GitExposureTest : public ::testing::Test {
protected:
    void SetUp() override {
        base_ = fs::temp_directory_path() /
                ("lcart-git-exposure-" + std::to_string(getpid()) + "-" +
                 std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        fs::remove_all(base_);
        fs::create_directories(repo() / ".git" / "info");
        base_ = fs::canonical(base_);
    }
    void TearDown() override { fs::remove_all(base_); }

    [[nodiscard]] fs::path repo() const { return base_ / "repo"; }

    static void write(const fs::path& file, std::string_view text) {
        fs::create_directories(file.parent_path());
        std::ofstream{file} << text;
    }

    /// The default database of a run in the repository root.
    [[nodiscard]] fs::path database() const { return repo() / "lcart-out" / "facts.lcart.db"; }

    [[nodiscard]] const fs::path& base() const { return base_; }

private:
    fs::path base_;
};

TEST_F(GitExposureTest, OutsideAnyWorkTree) {
    EXPECT_EQ(unignored_git_work_tree(base() / "elsewhere" / "facts.lcart.db", false),
              std::nullopt);
    EXPECT_EQ(unignored_git_work_tree(repo() / "facts.lcart.db", false), repo()); // control
}

TEST_F(GitExposureTest, InsideWorkTreeWithoutIgnoreFiles) {
    EXPECT_EQ(unignored_git_work_tree(database(), false), repo());
    EXPECT_EQ(unignored_git_work_tree(repo() / "lcart-out", true), repo());
}

TEST_F(GitExposureTest, DirectoryPatternIgnoresEverythingBelow) {
    write(repo() / ".gitignore", "# outputs\n\nlcart-out/\n");
    EXPECT_EQ(unignored_git_work_tree(database(), false), std::nullopt);
    EXPECT_EQ(unignored_git_work_tree(repo() / "lcart-out", true), std::nullopt);
    // Without a leading or middle slash it matches at any depth.
    EXPECT_EQ(unignored_git_work_tree(repo() / "sub" / "lcart-out" / "x.db", false), std::nullopt);
    EXPECT_EQ(unignored_git_work_tree(repo() / "other" / "x.db", false), repo());
}

TEST_F(GitExposureTest, DirectoryPatternDoesNotMatchAFile) {
    write(repo() / ".gitignore", "facts.lcart.db/\n");
    EXPECT_EQ(unignored_git_work_tree(database(), false), repo());
}

TEST_F(GitExposureTest, FileNamePatternAtAnyDepth) {
    write(repo() / ".gitignore", "*.lcart.db\n");
    EXPECT_EQ(unignored_git_work_tree(database(), false), std::nullopt);
    EXPECT_EQ(unignored_git_work_tree(repo() / "a" / "b" / "c.lcart.db", false), std::nullopt);
    EXPECT_EQ(unignored_git_work_tree(repo() / "facts.db", false), repo());
}

TEST_F(GitExposureTest, AnchoredPatternsMatchFromTheirDirectory) {
    write(repo() / ".gitignore", "/lcart-out\nbuild/out/\n");
    EXPECT_EQ(unignored_git_work_tree(database(), false), std::nullopt);
    EXPECT_EQ(unignored_git_work_tree(repo() / "sub" / "lcart-out" / "x.db", false), repo());
    EXPECT_EQ(unignored_git_work_tree(repo() / "build" / "out" / "x.db", false), std::nullopt);
    EXPECT_EQ(unignored_git_work_tree(repo() / "sub" / "build" / "out" / "x.db", false), repo());
}

TEST_F(GitExposureTest, NestedIgnoreFileAppliesBelowItsDirectory) {
    write(repo() / "sub" / ".gitignore", "*.db\n");
    EXPECT_EQ(unignored_git_work_tree(repo() / "sub" / "x.db", false), std::nullopt);
    EXPECT_EQ(unignored_git_work_tree(repo() / "x.db", false), repo());
}

TEST_F(GitExposureTest, LaterNegationReincludesAFile) {
    write(repo() / ".gitignore", "*.db\n");
    write(repo() / "lcart-out" / ".gitignore", "!facts.lcart.db\n");
    EXPECT_EQ(unignored_git_work_tree(database(), false), repo());
}

TEST_F(GitExposureTest, NegationCannotReincludeBelowAnIgnoredDirectory) {
    write(repo() / ".gitignore", "lcart-out/\n!lcart-out/facts.lcart.db\n");
    EXPECT_EQ(unignored_git_work_tree(database(), false), std::nullopt);
    EXPECT_EQ(unignored_git_work_tree(repo() / "facts.lcart.db", false), repo());
}

// Expected values in the next three tests are those of
// `git check-ignore --no-index` (git 2.43).
TEST_F(GitExposureTest, TrailingDoubleStarDoesNotExcludeTheDirectoryItself) {
    write(repo() / ".gitignore", "lcart-out/**\n!lcart-out/facts.lcart.db\n");
    EXPECT_EQ(unignored_git_work_tree(database(), false), repo());
    write(repo() / ".gitignore", "lcart-out/**\n");
    EXPECT_EQ(unignored_git_work_tree(database(), false), std::nullopt);
    EXPECT_EQ(unignored_git_work_tree(repo() / "lcart-out", true), repo());
}

TEST_F(GitExposureTest, PosixCharacterClassCountsAsUncertain) {
    write(repo() / ".gitignore", "*.db\n!facts.lcart.[[:alpha:]]b\n");
    EXPECT_EQ(unignored_git_work_tree(database(), false), repo());
    write(repo() / ".gitignore", "*.db\n");
    EXPECT_EQ(unignored_git_work_tree(database(), false), std::nullopt); // control
}

TEST_F(GitExposureTest, ByteOrderMarkBeforeTheFirstLineIsSkipped) {
    write(repo() / ".git" / "info" / "exclude", "*.db\n");
    write(repo() / ".gitignore", "\xEF\xBB\xBF!facts.lcart.db\n");
    EXPECT_EQ(unignored_git_work_tree(database(), false), repo());
    write(repo() / ".gitignore", "\xEF\xBB\xBFlcart-out/\n");
    write(repo() / ".git" / "info" / "exclude", "");
    EXPECT_EQ(unignored_git_work_tree(database(), false), std::nullopt);
}

TEST_F(GitExposureTest, NonAsciiNegationCountsAsUncertain) {
    // Not a git behaviour: non-ASCII patterns are left to git, so the check
    // cannot conclude "ignored" past one that negates.
    write(repo() / ".gitignore", "lcart-out/\n!d\xC3\xA9j\xC3\xA0\n");
    EXPECT_EQ(unignored_git_work_tree(database(), false), repo());
}

TEST_F(GitExposureTest, InfoExcludeIsRead) {
    write(repo() / ".git" / "info" / "exclude", "lcart-out/\n");
    EXPECT_EQ(unignored_git_work_tree(database(), false), std::nullopt);
    EXPECT_EQ(unignored_git_work_tree(repo() / "facts.lcart.db", false), repo());
}

TEST_F(GitExposureTest, GitFileMarksAWorkTreeToo) {
    const fs::path worktree = base() / "worktree";
    write(worktree / ".git", "gitdir: ../repo/.git/worktrees/w\n");
    EXPECT_EQ(unignored_git_work_tree(worktree / "lcart-out" / "facts.lcart.db", false), worktree);
    write(worktree / ".gitignore", "lcart-out/\n");
    EXPECT_EQ(unignored_git_work_tree(worktree / "lcart-out" / "facts.lcart.db", false),
              std::nullopt);
}

TEST_F(GitExposureTest, UnsupportedNegationCountsAsNotIgnored) {
    // A negation the matcher cannot read might re-include the file: warn.
    write(repo() / ".gitignore", "lcart-out/\n!weird\\*name\n");
    EXPECT_EQ(unignored_git_work_tree(database(), false), repo());
}

TEST_F(GitExposureTest, UnsupportedPatternIsSkipped) {
    // Skipping a pattern can only turn "ignored" into a warning.
    write(repo() / ".gitignore", "[^x]*.db\nlcart-out/\n");
    EXPECT_EQ(unignored_git_work_tree(database(), false), std::nullopt);
    write(repo() / ".gitignore", "lcart-\\out/\n");
    EXPECT_EQ(unignored_git_work_tree(database(), false), repo());
}

TEST_F(GitExposureTest, SymbolicLinkIgnoreFileIsNotFollowed) {
    write(repo() / "rules", "lcart-out/\n");
    fs::create_symlink(repo() / "rules", repo() / ".gitignore");
    EXPECT_EQ(unignored_git_work_tree(database(), false), repo());
}

TEST_F(GitExposureTest, OversizedIgnoreFileCountsAsUncertain) {
    write(repo() / ".git" / "info" / "exclude", "lcart-out/\n");
    // Over 1 MiB: not read, and it might re-include the database.
    write(repo() / ".gitignore", std::string(1024UL * 1024UL + 1, '#'));
    EXPECT_EQ(unignored_git_work_tree(database(), false), repo());
    write(repo() / ".gitignore", "# small\n");
    EXPECT_EQ(unignored_git_work_tree(database(), false), std::nullopt); // control
}

TEST_F(GitExposureTest, FifoIgnoreFileIsNotOpened) {
    write(repo() / ".git" / "info" / "exclude", "lcart-out/\n");
    // Opening a FIFO would block until a writer appears.
    ASSERT_EQ(mkfifo((repo() / ".gitignore").c_str(), 0600), 0);
    EXPECT_EQ(unignored_git_work_tree(database(), false), repo());
}

TEST_F(GitExposureTest, DirectoryNamedLikeAnIgnoreFileCountsAsUncertain) {
    write(repo() / ".git" / "info" / "exclude", "lcart-out/\n");
    fs::create_directories(repo() / ".gitignore");
    EXPECT_EQ(unignored_git_work_tree(database(), false), repo());
}

TEST_F(GitExposureTest, UnreadableIgnoreFileCountsAsUncertain) {
    if (geteuid() == 0) {
        GTEST_SKIP() << "root can read a file without read permission";
    }
    write(repo() / ".git" / "info" / "exclude", "lcart-out/\n");
    write(repo() / ".gitignore", "!lcart-out/facts.lcart.db\n");
    fs::permissions(repo() / ".gitignore", fs::perms::none);
    EXPECT_EQ(unignored_git_work_tree(database(), false), repo());
    fs::permissions(repo() / ".gitignore", fs::perms::owner_read | fs::perms::owner_write);
}

TEST_F(GitExposureTest, TrailingSpacesAndCarriageReturnsAreTrimmed) {
    write(repo() / ".gitignore", "lcart-out/   \r\n");
    EXPECT_EQ(unignored_git_work_tree(database(), false), std::nullopt);
    EXPECT_EQ(unignored_git_work_tree(repo() / "facts.lcart.db", false), repo());
}

TEST_F(GitExposureTest, RelativePathIsResolvedAgainstTheWorkingDirectory) {
    const fs::path previous = fs::current_path();
    fs::current_path(repo());
    const auto found = unignored_git_work_tree("lcart-out/facts.lcart.db", false);
    fs::current_path(previous);
    EXPECT_EQ(found, repo());
}

} // namespace
