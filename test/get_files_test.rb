require File.expand_path(File.join(File.dirname(__FILE__), 'test_helper'))

class GetFilesTest < Verm::TestCase
  def test_gives_404_for_nonexistant_paths
    assert_statistics_change(:get_requests => 2, :get_requests_not_found => 2) do
      copy_arbitrary_file_to('somefiles', nil)
      get :path => "/somefiles/x#{@filename}y",
          :expected_response_code => 404
      get :path => "/otherdirectory/#{@filename}",
          :expected_response_code => 404
    end
  end
  
  def test_serves_files_with_no_extension_with_generic_content_type
    copy_arbitrary_file_to('somefiles', nil)
    get :path => @location,
        :expected_content_type => "application/octet-stream"
  end
  
  def test_serves_files_with_unknown_extension_with_generic_content_type
    copy_arbitrary_file_to('somefiles', 'foobarext')
    get :path => @location,
        :expected_content_type => "application/octet-stream"
  end
  
  def test_serves_files_with_txt_extension_as_text_plain
    copy_arbitrary_file_to('somefiles', 'txt')
    get :path => @location,
        :expected_content_type => "text/plain"
  end
  
  def test_serves_files_with_jpg_extension_as_image_jpeg
    copy_arbitrary_file_to('somefiles', 'jpg')
    get :path => @location,
        :expected_content_type => "image/jpeg"
  end
  
  def test_serves_files_with_extension_from_custom_mime_types_file
    copy_arbitrary_file_to('somefiles', 'vermtest1')
    get :path => @location,
        :expected_content_type => "application/verm-test-file"

    copy_arbitrary_file_to('somefiles', 'vermtest2')
    get :path => @location,
        :expected_content_type => "application/verm-test-file"

    copy_arbitrary_file_to('somefiles', 'vermother')
    get :path => @location,
        :expected_content_type => "application/verm-other-file"
  end
  
  def test_serves_files_with_correct_length_and_content
    assert_statistics_change(:get_requests => 1) do
      copy_arbitrary_file_to('somefiles', nil)
      File.open(@original_file, 'rb') do |f|
        get :path => @location,
            :expected_content_length => f.stat.size,
            :expected_content => f.read
      end
    end
  end
  
  def test_serves_files_with_etag_and_supports_if_none_match
    assert_statistics_change(:get_requests => 2) do
      copy_arbitrary_file_to('somefiles', nil)
      response = get :path => @location
      assert_not_nil response['etag']
      
      get :path => @location,
          :headers => {'if-none-match' => response['etag']},
          :expected_response_code => 304, # HTTP not modified
          :expected_content => nil # and body not sent
    end
  end
  
  def test_serves_files_uncompressed_if_client_accepts_gzip_but_file_is_uncompressed
    copy_arbitrary_file_to('somefiles', 'vermtest1', compressed: false)
    File.open(@original_file, 'rb') do |f|
      get :path => @location,
          :accept_encoding => 'gzip, deflate',
          :expected_content_encoding => nil, # not compressed
          :expected_content_type => 'application/verm-test-file',
          :expected_content => f.read
    end
  end
  
  def test_serves_files_compressed_if_client_accepts_gzip_and_file_is_compressed
    copy_arbitrary_file_to('somefiles', 'vermtest1', compressed: true)
    File.open(@original_file, 'rb') do |f|
      get :path => @location,
          :accept_encoding => 'gzip, deflate',
          :expected_content_encoding => 'gzip', # compressed
          :expected_content_type => 'application/verm-test-file',
          :expected_content => f.read
    end
  end
  
  def test_serves_files_compressed_if_client_accept_header_not_present_and_file_is_compressed
    copy_arbitrary_file_to('somefiles', 'vermtest1', compressed: true)
    File.open(@original_file, 'rb') do |f|
      get :path => @location,
          :accept_encoding => nil,
          :expected_content_encoding => 'gzip', # compressed
          :expected_content_type => 'application/verm-test-file',
          :expected_content => f.read
    end
  end
  
  def test_serves_files_compressed_without_content_type_if_no_extension_and_file_is_compressed
    copy_arbitrary_file_to('somefiles', nil, compressed: true)
    File.open(@original_file, 'rb') do |f|
      get :path => @location,
          :accept_encoding => nil,
          :expected_content_encoding => 'gzip', # compressed
          :expected_content_type => 'application/octet-stream', # must not be gzip again, as that would mean a double-compressed file, which is not the case
          :expected_content => f.read
    end
  end

  def test_serves_files_decompressed_if_client_does_not_accept_gzip_and_file_is_compressed
    copy_arbitrary_file_to('somefiles', 'vermtest1', compressed: true)
    File.open(@original_file.gsub('.gz', ''), 'rb') do |f|
      get :path => @location,
          :accept_encoding => 'foo',
          :expected_content_encoding => nil,
          :expected_content_type => 'application/verm-test-file',
          :expected_content => f.read
    end
  end

  def test_serves_files_compressed_if_client_requests_gz
    copy_arbitrary_file_to('somefiles', 'vermtest1', compressed: true)
    File.open(@original_file, 'rb') do |f|
      get :path => "#{@location}.gz",
          :expected_content_encoding => nil,
          :expected_content_type => 'application/gzip',
          :expected_content => f.read
    end
  end
end
