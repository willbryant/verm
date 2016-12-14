require File.expand_path(File.join(File.dirname(__FILE__), 'test_helper'))
require File.expand_path(File.join(File.dirname(__FILE__), 'create_files_tests'))

class CreateFilesRawTest < Verm::TestCase
  include CreateFilesSharedTests

  def test_saves_gzip_content_encoded_files_as_gzipped_but_hashes_uncompressed_content
    location_uncompressed =
      post_file :path => '/foo',
                :file => 'simple_text_file',
                :type => 'text/plain',
                :expected_extension => 'txt'

    location_compressed =
      post_file :path => '/foo',
                :file => 'simple_text_file.gz',
                :type => 'text/plain',
                :encoding => 'gzip',
                :expected_extension => 'txt', # this is the expected extension of the path
                :expected_extension_suffix => 'gz' # but this is further expected on the filename
    
    assert_equal location_uncompressed, location_compressed # hash must be based on the content, not the encoded content
  end

  def test_saves_same_gzipped_file_compressed_differently_to_same_path
    recompressed = gzip(fixture_file_data('simple_text_file'))
    refute_equal recompressed, fixture_file_data('simple_text_file.gz')

    location1 =
      post_file :path => '/foo',
                :file => 'simple_text_file.gz',
                :type => 'text/plain',
                :encoding => 'gzip',
                :expected_extension => 'txt', # this is the expected extension of the path
                :expected_extension_suffix => 'gz' # but this is further expected on the filename

    location2 =
      post_file :path => '/foo',
                :data => recompressed,
                :type => 'text/plain',
                :encoding => 'gzip',
                :expected_extension => 'txt', # this is the expected extension of the path
                :expected_extension_suffix => 'gz' # but this is further expected on the filename

    assert_equal location1, location2
  end

  def test_saves_gzip_content_encoded_gzip_files_as_gzipped_once_and_hashes_uncompressed_content
    location_uncompressed =
      post_file :path => '/foo',
                :file => 'binary_file',
                :type => 'application/octet-stream',
                :expected_extension => ''

    location_compressed =
      post_file :path => '/foo',
                :data => gzip(fixture_file_data('binary_file.gz')), # so the content has been twice-compressed
                :expected_data => fixture_file_data('binary_file.gz'),
                :type => 'application/gzip',
                :encoding => 'gzip',
                :expected_extension => 'gz',
                :expected_extension_suffix => nil
    
    assert_equal location_uncompressed + ".gz", location_compressed # hash must be based on the content, not the encoded content
  end
end
