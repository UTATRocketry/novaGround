#include <boost/test/unit_test.hpp>

#include <cstdlib>

#include "core/data_file.hpp"
#include "temp_dir.hpp"

BOOST_AUTO_TEST_SUITE(data_file)

BOOST_AUTO_TEST_CASE(writes_lines_and_appends_across_instances) {
    TempDir dir;
    auto path = (dir.path() / "out.log").string();
    {
        DataFileWriter w(path);
        BOOST_TEST(w.ok());
        w.write_line("one");
        w.write_line("two");
    }
    {
        DataFileWriter w(path);
        w.write_line("three");
    }
    BOOST_TEST(read_file(path) == "one\ntwo\nthree\n");
}

BOOST_AUTO_TEST_CASE(unopenable_path_disables_the_writer) {
    TempDir dir;
    DataFileWriter w((dir.path() / "missing" / "out.log").string());
    BOOST_TEST(!w.ok());
    w.write_line("ignored");  // must not crash
}

BOOST_AUTO_TEST_CASE(from_env_returns_null_when_unset_or_empty) {
    unsetenv("NOVA_TEST_DATA_FILE");
    BOOST_TEST(DataFileWriter::from_env("NOVA_TEST_DATA_FILE") == nullptr);
    setenv("NOVA_TEST_DATA_FILE", "", 1);
    BOOST_TEST(DataFileWriter::from_env("NOVA_TEST_DATA_FILE") == nullptr);
    unsetenv("NOVA_TEST_DATA_FILE");
}

BOOST_AUTO_TEST_CASE(from_env_opens_the_named_file) {
    TempDir dir;
    auto path = (dir.path() / "env.log").string();
    setenv("NOVA_TEST_DATA_FILE", path.c_str(), 1);
    auto w = DataFileWriter::from_env("NOVA_TEST_DATA_FILE");
    unsetenv("NOVA_TEST_DATA_FILE");
    BOOST_REQUIRE(w != nullptr);
    BOOST_TEST(w->ok());
}

BOOST_AUTO_TEST_SUITE_END()
