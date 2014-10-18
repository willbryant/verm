require File.expand_path(File.join(File.dirname(__FILE__), 'test_helper'))

class ReplicationPutTest < Verm::TestCase
  def assert_wrong_path
    yield
    fail "Expected a 422 Unprocessable Entity error"
  rescue Net::HTTPServerException => e
    assert e.response.is_a?(Net::HTTPUnprocessableEntity)
  end

  def test_saves_files_under_requested_path
    assert_statistics_change(:put_requests => 2, :put_requests_new_file_stored => 1) do
      put_file :path => '/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw',
               :file => 'simple_text_file',
               :type => 'application/octet-stream'

      put_file :path => '/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw',
               :file => 'simple_text_file',
               :type => 'application/octet-stream'
    end

    assert_statistics_change(:put_requests => 2, :put_requests_new_file_stored => 2) do
      put_file :path => '/foo/RL/Y1CmWD8NjaaUzE2Mnr-bd_8fTYcgfjC3279aQwxl9',
               :file => 'another_text_file',
               :type => 'application/octet-stream'

      put_file :path => '/different/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw',
               :file => 'simple_text_file',
               :type => 'application/octet-stream'
    end
  end

  def test_saves_binary_files_without_truncation_or_miscoding
    put_file :path => '/foo/IF/P8unS2JIuR6_UZI5pZ0lxWHhfvR2ocOcRAma_lEiA',
             :file => 'binary_file',
             :type => 'application/octet-stream'
  end
  
  def test_saves_unknown_type_files_without_complaining
    put_file :path => '/foo/IF/P8unS2JIuR6_UZI5pZ0lxWHhfvR2ocOcRAma_lEiA',
             :file => 'binary_file',
             :type => 'unknown/type'
  end

  def test_saves_text_plain_files_with_suitable_extension
    put_file :path => '/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw.txt',
             :file => 'simple_text_file',
             :type => 'text/plain'
  end

  def test_saves_text_html_files_with_suitable_extension
    put_file :path => '/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw.html',
             :file => 'simple_text_file',
             :type => 'text/html'
  end

  def test_saves_image_jpeg_files_with_suitable_extension
    put_file :path => '/foo/H1/M8Qv1eQpNp-H6c5IXGExEweX4rYJlIB4IqlAofjbN.jpg',
             :file => 'jpeg',
             :type => 'image/jpeg'
  end

  def test_saves_gzip_content_encoded_files_as_gzipped_but_returns_non_gzipped_path
    put_file :path => '/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw.txt',
             :file => 'simple_text_file.gz',
             :type => 'text/plain',
             :encoding => 'gzip',
             :expected_extension_suffix => 'gz' # this is further expected on the filename
  end

  def test_no_save_files_under_wrong_path
    assert_wrong_path do
      put_file :path => '/foo/Sn/Ui3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOx',
               :file => 'simple_text_file',
               :type => 'application/octet-stream'
    end

    assert_wrong_path do
      put_file :path => '/foo/SN/Ui3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOx',
               :file => 'simple_text_file',
               :type => 'application/octet-stream'
    end

    assert_wrong_path do
      put_file :path => '/foo/sn/Ui3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOx',
               :file => 'simple_text_file',
               :type => 'application/octet-stream'
    end

    assert_wrong_path do
      put_file :path => '/foo/Sn/ui3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOx',
               :file => 'simple_text_file',
               :type => 'application/octet-stream'
    end

    assert_wrong_path do
      put_file :path => '/foo/Sn_Ui3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOx',
               :file => 'simple_text_file',
               :type => 'application/octet-stream'
    end

    assert_wrong_path do
      put_file :path => '/foo/Tn/Ui3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOx',
               :file => 'simple_text_file',
               :type => 'application/octet-stream'
    end

    assert_wrong_path do
      put_file :path => '/foo/Sn/Ui3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOwy',
               :file => 'simple_text_file',
               :type => 'application/octet-stream'
    end

    put_file :path => '/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw',
             :file => 'simple_text_file',
             :type => 'application/octet-stream'

    put_file :path => '/different/path/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw',
             :file => 'simple_text_file',
             :type => 'application/octet-stream'

    put_file :path => '/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw.ext',
             :file => 'simple_text_file',
             :type => 'application/octet-stream'
  end
end
