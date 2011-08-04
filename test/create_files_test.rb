require File.expand_path(File.join(File.dirname(__FILE__), 'test_helper'))

class CreateFilesTest < Verm::TestCase
  def test_saves_files_under_requested_path
    post_file :path => '/foo',
              :file => 'simple_text_file',
              :type => 'application/octet-stream',
              :expected_extension => nil

    post_file :path => '/foo',
              :file => 'another_text_file',
              :type => 'application/octet-stream',
              :expected_extension => nil

    post_file :path => '/different',
              :file => 'simple_text_file',
              :type => 'application/octet-stream',
              :expected_extension => nil
  end

  def test_saves_binary_files_without_truncation_or_miscoding
    post_file :path => '/foo',
              :file => 'binary_file',
              :type => 'application/octet-stream',
              :expected_extension => nil
  end
  
  def test_saves_unknown_type_files_without_complaining
    post_file :path => '/foo',
              :file => 'binary_file',
              :type => 'unknown/type',
              :expected_extension => nil
  end

  def test_saves_text_plain_files_with_suitable_extension
    post_file :path => '/foo',
              :file => 'simple_text_file',
              :type => 'text/plain',
              :expected_extension => 'txt'
  end

  def test_saves_text_html_files_with_suitable_extension
    post_file :path => '/foo',
              :file => 'simple_text_file',
              :type => 'text/html',
              :expected_extension => 'html'
  end

  def test_saves_image_jpeg_files_with_suitable_extension
    post_file :path => '/foo',
              :file => 'jpeg',
              :type => 'image/jpeg',
              :expected_extension => 'jpg'
  end
  
  def test_saves_same_file_to_same_path
    first_file_location =
      post_file(:path => '/foo',
                :file => 'simple_text_file',
                :type => 'application/octet-stream',
                :expected_extension => nil)
    assert_equal first_file_location,
      post_file(:path => '/foo',
                :file => 'simple_text_file',
                :type => 'application/octet-stream',
                :expected_extension => nil)

    different_file_location = 
      post_file(:path => '/foo',
                :file => 'another_text_file',
                :type => 'application/octet-stream',
                :expected_extension => nil)
    assert_equal different_file_location,
      post_file(:path => '/foo',
                :file => 'another_text_file',
                :type => 'application/octet-stream',
                :expected_extension => nil)
    
    assert_not_equal first_file_location, different_file_location
  end
end
