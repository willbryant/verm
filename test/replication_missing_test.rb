require File.expand_path(File.join(File.dirname(__FILE__), 'test_helper'))

class ReplicationMissingTest < Verm::TestCase
  def paths
    @paths ||= ["/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw\r\n", "/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw.txt\r\n"]
  end

  def gzip(str)
    output = StringIO.new("".force_encoding("binary"))
    gz = Zlib::GzipWriter.new(output)
    gz.write(str)
    gz.close
    output.string
  end

  def ungzip(str)
    input = StringIO.new(str.force_encoding("binary"))
    Zlib::GzipReader.new(input).read
  end

  def assert_gzipped_body(content, response)
    assert_equal 'gzip', response['content-encoding']
    assert_equal content, ungzip(response.body)
  end

  def assert_ungzipped_body(content, response)
    assert_equal nil, response['content-encoding']
    assert_equal content, response.body
  end

  def test_echos_missing_files
    assert_gzipped_body paths.join, put(:path => "/_missing", :data => paths.join)

    put_file :path => '/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw',
             :file => 'simple_text_file',
             :type => 'application/octet-stream'

    assert_gzipped_body paths[1..-1].join, put(:path => "/_missing", :data => paths.join)

    put_file :path => '/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw.txt',
             :file => 'simple_text_file',
             :type => 'text/plain'

    assert_gzipped_body paths[2..-1].join, put(:path => "/_missing", :data => paths.join)
  end

  def test_looks_for_compressed_files
    assert_gzipped_body paths.join, put(:path => "/_missing", :data => paths.join)

    put_file :path => '/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw.txt',
             :file => 'simple_text_file.gz',
             :type => 'text/plain',
             :encoding => 'gzip',
             :expected_extension_suffix => 'gz' # this is further expected on the filename

    assert_gzipped_body (paths[0..0] + paths[2..-1]).join, put(:path => "/_missing", :data => paths.join)
  end

  def test_supports_compressed_requests
    assert_gzipped_body paths.join, put(:path => "/_missing", :data => gzip(paths.join), :encoding => "gzip")

    put_file :path => '/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw',
             :file => 'simple_text_file',
             :type => 'application/octet-stream'

    assert_gzipped_body paths[1..-1].join, put(:path => "/_missing", :data => gzip(paths.join), :encoding => "gzip")

    put_file :path => '/foo/Sn/Ei3p5f7Ht82wQtJ_PG7ostrwGnYwPZatsV0T3HBOw.txt',
             :file => 'simple_text_file',
             :type => 'text/plain'

    assert_gzipped_body paths[2..-1].join, put(:path => "/_missing", :data => gzip(paths.join), :encoding => "gzip")
  end

  def test_supports_uncompressed_responses
    assert_gzipped_body   paths.join, put(:path => "/_missing", :data => paths.join)
    assert_gzipped_body   paths.join, put(:path => "/_missing", :data => paths.join, :accept_encoding => 'foo,gzip,bar')
    assert_gzipped_body   paths.join, put(:path => "/_missing", :data => paths.join, :accept_encoding => 'gzip,bar')
    assert_ungzipped_body paths.join, put(:path => "/_missing", :data => paths.join, :accept_encoding => 'foo,bar')
  end
end
