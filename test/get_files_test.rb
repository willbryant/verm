require File.expand_path(File.join(File.dirname(__FILE__), 'test_helper'))

class GetFilesTest < Verm::TestCase
  def copy_file_to(directory, extension)
    @arbitrary_file = 'binary_file'
    @original_file = File.join(File.dirname(__FILE__), 'fixtures', @arbitrary_file)
    @hash_of_file = '_y6dLYki5Hr9RkjmlnSXFYeF-9Hahw5xECZr-USIAA' # happens to be correct, but not relevant to the tests
    @filename = extension ? "#{@hash_of_file}.#{extension}" : @hash_of_file
    
    @dest_directory = File.join(File.dirname(__FILE__), 'data', directory)
    FileUtils.mkdir(@dest_directory) unless File.directory?(@dest_directory)
    FileUtils.cp(@original_file, File.join(@dest_directory, @filename))
  end
  
  def test_gives_404_for_nonexistant_paths
    copy_file_to('somefiles', nil)
    get :path => "/somefiles/x#{@filename}y",
        :expected_response_code => 404
    get :path => "/otherdirectory/#{@filename}",
        :expected_response_code => 404
  end
  
  def test_serves_files_with_no_extension_without_content_type
    copy_file_to('somefiles', nil)
    get :path => "/somefiles/#{@filename}",
        :expected_content_type => ""
  end
  
  def test_serves_files_with_unknown_extension_without_content_type
    copy_file_to('somefiles', 'foobarext')
    get :path => "/somefiles/#{@filename}",
        :expected_content_type => ""
  end
  
  def test_serves_files_with_txt_extension_as_text_plain
    copy_file_to('somefiles', 'txt')
    get :path => "/somefiles/#{@filename}",
        :expected_content_type => "text/plain"
  end
  
  def test_serves_files_with_jpg_extension_as_image_jpeg
    copy_file_to('somefiles', 'jpg')
    get :path => "/somefiles/#{@filename}",
        :expected_content_type => "image/jpeg"
  end
  
  def test_serves_files_with_extension_from_custom_mime_types_file
    copy_file_to('somefiles', 'vermtest1')
    get :path => "/somefiles/#{@filename}",
        :expected_content_type => "application/verm-test-file"

    copy_file_to('somefiles', 'vermtest2')
    get :path => "/somefiles/#{@filename}",
        :expected_content_type => "application/verm-test-file"

    copy_file_to('somefiles', 'vermother')
    get :path => "/somefiles/#{@filename}",
        :expected_content_type => "application/verm-other-file"
  end
  
  def test_serves_files_with_correct_length_and_content
    copy_file_to('somefiles', nil)
    File.open(@original_file, 'rb') do |f|
      get :path => "/somefiles/#{@filename}",
          :expected_content_length => f.stat.size,
          :expected_content => f.read
    end
  end
end
