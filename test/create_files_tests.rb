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

  def test_saves_files_with_configured_extension
    post_file :path => '/foo',
              :file => 'simple_text_file',
              :type => 'application/verm-test-file',
              :expected_extension => 'vermtest1'

    post_file :path => '/foo',
              :file => 'simple_text_file',
              :type => 'application/verm-other-file',
              :expected_extension => 'vermother'
  end

  def test_saves_files_with_builtin_extension_in_preference_to_configured_extension_by_default
    post_file :path => '/foo',
              :file => 'simple_text_file',
              :type => 'text/plain',
              :expected_extension => 'txt'
  end

  def test_saves_files_with_configured_extension_if_forced
    teardown_verm
    spawn_verm "no-default-mime-types" => ""

    post_file :path => '/foo',
              :file => 'simple_text_file',
              :type => 'text/plain',
              :expected_extension => 'badidea'
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
      
      refute_equal first_file_location, different_file_location
    end
  end

  # see also test_saves_gzip_content_encoded_files_as_gzipped_but_returns_non_gzipped_path in
  # create_files_raw_test.rb, which is about gzip content encoding rather than gzip files
  def test_saves_gzip_files_as_gzipped_but_returns_non_gzipped_path
    location_uncompressed =
      post_file :path => '/foo',
                :file => 'simple_text_file',
                :type => 'application/octet-stream'

    location_compressed =
      post_file :path => '/foo',
                :file => 'simple_text_file.gz',
                :type => 'application/x-gzip',
                :expected_extension => 'gz' # note not expected_extension_suffix - we uploaded as a gzip file not a content-encoded plain file

    assert_equal location_uncompressed + ".gz", location_compressed # hash must be based on the content, not the encoded content
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


  def count_tempfiles_in(dir)
    Dir["#{dir}/_upload*"].size
  end

  def test_cleans_tempfiles_on_abort
    path = "/abort-test"
    dir = File.join(default_verm_spawner.verm_data, path)
    socket = TCPSocket.new(default_verm_spawner.hostname, default_verm_spawner.port)
    socket.puts "POST #{path} HTTP/1.0"
    socket.puts "Content-Length: 100000"
    socket.puts ""
    socket.puts "foo"
    repeatedly_wait_until { count_tempfiles_in(dir) > 0 }
    socket.close
    repeatedly_wait_until { count_tempfiles_in(dir) == 0 }
    assert_equal [], Dir["#{dir}/*"]
  end

  def test_cleans_tempfiles_on_shutdown
    path = "/abort-test"
    dir = File.join(default_verm_spawner.verm_data, path)
    socket = TCPSocket.new(default_verm_spawner.hostname, default_verm_spawner.port)
    socket.puts "POST #{path} HTTP/1.0"
    socket.puts "Content-Length: 100000"
    socket.puts ""
    socket.puts "foo"
    repeatedly_wait_until { count_tempfiles_in(dir) > 0 }
    default_verm_spawner.stop_verm
    assert_equal 0, count_tempfiles_in(dir)
    assert_equal [], Dir["#{dir}/*"]
  end
end
