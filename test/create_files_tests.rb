require File.expand_path(File.join(File.dirname(__FILE__), 'test_helper'))

module CreateFilesSharedTests
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

    post_file :path => '/foo',
              :file => 'medium_file',
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
    assert_statistics_change(:post_requests => 4, :post_requests_new_file_stored => 2) do
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

  def test_rejects_mismatching_files
    location =
      post_file(:path => '/foo',
                :file => 'simple_text_file',
                :type => 'text/plain',
                :expected_extension => nil)
    dest_filename = expected_filename(location)
    File.open(dest_filename, "wb") {|f| f.write("different")}
    assert_equal location.gsub(".txt", "_2.txt"),
      post_file(:path => '/foo',
                :file => 'simple_text_file',
                :type => 'text/plain',
                :expected_extension => nil)
  end

  def test_rejects_mismatching_shorter_files
    location =
      post_file(:path => '/foo',
                :file => 'simple_text_file',
                :type => 'text/plain',
                :expected_extension => nil)
    dest_filename = expected_filename(location)
    File.open(dest_filename, "wb") {|f| f.truncate(20)}
    assert_equal location.gsub(".txt", "_2.txt"),
      post_file(:path => '/foo',
                :file => 'simple_text_file',
                :type => 'text/plain',
                :expected_extension => nil)
  end

  def test_rejects_mismatching_longer_files
    location =
      post_file(:path => '/foo',
                :file => 'simple_text_file',
                :type => 'text/plain',
                :expected_extension => nil)
    dest_filename = expected_filename(location)
    File.open(dest_filename, "ab") {|f| f.write("extra")}
    assert_equal location.gsub(".txt", "_2.txt"),
      post_file(:path => '/foo',
                :file => 'simple_text_file',
                :type => 'text/plain',
                :expected_extension => nil)
  end
end
